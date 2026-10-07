using System;
using System.Collections;
using System.Collections.Generic;
using System.Globalization;
using System.Linq;
using System.Reflection;
using Il2CppInterop.Runtime;
using Il2CppInterop.Runtime.InteropTypes;
using SimpleJSON;
using UnityEngine;

namespace UnityTools
{
    internal sealed class Il2CppObjectAccess
    {
        private sealed class Reference
        {
            public object value;
            public string parent;
            public JSONArray path;
            public int sceneHandle;
            public string hierarchy;
        }
        private sealed class Observation
        {
            public string root;
            public string path;
            public List<object> chain;
            public List<CollectionObservation> collections;
            public Type declaredType;
        }
        private sealed class CollectionObservation
        {
            public int depth;
            public int count;
            public object version;
            public object entry;
        }
        private sealed class MethodBinding { public string root; public MethodInfo method; }
        private readonly Dictionary<string, Reference> references = new Dictionary<string, Reference>();
        private readonly Dictionary<string, Observation> observations = new Dictionary<string, Observation>();
        private readonly Dictionary<string, MethodBinding> methods = new Dictionary<string, MethodBinding>();
        private readonly Dictionary<string, Type> projections = new Dictionary<string, Type>();
        private static readonly BindingFlags Flags = BindingFlags.Instance | BindingFlags.Static | BindingFlags.Public | BindingFlags.NonPublic;
        private static readonly MethodInfo CastMethod = typeof(Il2CppObjectBase).GetMethods().Single(m => m.Name == "TryCast" && m.IsGenericMethodDefinition);

        internal Action<object, string> ObjectObserved;
        internal object ResolveForBridge(string id) { try { return Target(id); } catch { return null; } }
        internal string RegisterForBridge(object value) { return Register(value); }
        internal object ProjectForBridge(object value) { return Project(value); }
        public void Clear() { references.Clear(); observations.Clear(); methods.Clear(); StaleReasons.Clear(); }
        private static readonly Dictionary<string, string> StaleReasons = new Dictionary<string, string>();
        private static Type RuntimeType(object value) { return value as Type ?? value.GetType(); }
        private static object Instance(object value) { return value is Type ? null : value; }
        internal static bool Alive(object value)
        {
            if (value == null) return false;
            var native = value as Il2CppObjectBase;
            if (native != null && native.WasCollected) return false;
            if (value is UnityEngine.Object) return (UnityEngine.Object)value != null;
            return true;
        }
        internal static bool Same(object a, object b)
        {
            var x = a as Il2CppObjectBase; var y = b as Il2CppObjectBase;
            if (x != null || y != null) return x != null && y != null && !x.WasCollected && !y.WasCollected && x.Pointer == y.Pointer;
            if (a == null || b == null) return a == b;
            return a.GetType().IsValueType || a is string ? Equals(a, b) : ReferenceEquals(a, b);
        }
        private object Project(object value)
        {
            var native = value as Il2CppSystem.Object;
            if (native == null) return value;
            if (!Alive(native)) throw new InvalidOperationException("stale-reference");
            var nativeName = native.GetIl2CppType().FullName;
            Type projection;
            if (!projections.TryGetValue(nativeName, out projection))
            {
                foreach (var assembly in AppDomain.CurrentDomain.GetAssemblies())
                {
                    projection = assembly.GetType(nativeName, false);
                    if (projection != null) break;
                }
                if (projection == null) return value;
                projections[nativeName] = projection;
            }
            if (projection.IsInstanceOfType(value)) return value;
            if (!typeof(Il2CppObjectBase).IsAssignableFrom(projection)) return value;
            return CastMethod.MakeGenericMethod(projection).Invoke(value, null) ?? value;
        }
        private static GameObject GameObjectOf(object value)
        {
            var native = value as Il2CppObjectBase;
            if (native == null || native.WasCollected) return null;
            var component = native.TryCast<Component>();
            return component != null ? component.gameObject : native.TryCast<GameObject>();
        }
        private static string Hierarchy(GameObject gameObject)
        {
            var path = gameObject.name;
            for (var parent = gameObject.transform.parent; parent != null; parent = parent.parent) path = parent.name + "/" + path;
            return path;
        }
        private string Register(object value, string parent = null, JSONArray path = null)
        {
            value = Project(value);
            if (!Alive(value)) throw new InvalidOperationException("stale-reference");
            var gameObject = GameObjectOf(value);
            var id = Guid.NewGuid().ToString("N");
            references[id] = new Reference { value = value, parent = parent, path = path == null ? null : JSON.Parse(path.ToString()).AsArray,
                sceneHandle = gameObject == null ? 0 : gameObject.scene.handle, hierarchy = gameObject == null ? null : Hierarchy(gameObject) };
            var observed = ObjectObserved;
            if (observed != null) { try { observed(value, id); } catch { } }
            return id;
        }
        private object Target(string id)
        {
            Reference record;
            if (String.IsNullOrEmpty(id) || !references.TryGetValue(id, out record)) throw new InvalidOperationException("stale-reference");
            var value = record.value;
            if (!Alive(value)) { StaleReasons[id] = "collected-or-destroyed"; throw new InvalidOperationException("stale-reference"); }
            if (!String.IsNullOrEmpty(record.parent))
            {
                var parent = Target(record.parent);
                var current = record.path == null ? null : Resolve(parent, record.path);
                if (current == null || !Same(current, value)) { StaleReasons[id] = "parent-reference-changed"; throw new InvalidOperationException("stale-reference"); }
            }
            var gameObject = GameObjectOf(value);
            if (record.hierarchy != null && (gameObject == null || !gameObject.scene.isLoaded || gameObject.scene.handle != record.sceneHandle || Hierarchy(gameObject) != record.hierarchy))
            { StaleReasons[id] = "scene-or-hierarchy-changed"; throw new InvalidOperationException("stale-reference"); }
            return Project(value);
        }
        public JSONNode Execute(string operation, JSONNode args)
        {
            bool started = false;
            try
            {
                if (args == null || !args.IsObject) throw new InvalidOperationException("arguments-must-be-object");
                if (operation == "find") return Find(args);
                if (operation == "release")
                {
                    if (args["all"].AsBool) Clear();
                    else
                    {
                        if (!String.IsNullOrEmpty(args["objectId"].Value)) ReleaseTree(args["objectId"].Value);
                        foreach (var id in args["objectIds"].Children) ReleaseTree(id.Value);
                        foreach (var id in args["snapshotIds"].Children) observations.Remove(id.Value);
                    }
                    return new JSONObject { ["state"] = "completed" };
                }
                var root = args["objectId"].Value;
                var target = Target(root);
                if (operation == "inspect") return Inspect(target, root);
                if (operation == "invoke") return Invoke(target, root, args, ref started);
                var path = Path(args); var chain = new List<object> { target }; var checks = new List<CollectionObservation>();
                object current = target;
                for (int depth = 0; depth < path.Count; ++depth)
                {
                    if (!Alive(current)) throw new InvalidOperationException("stale-reference");
                    if (!path[depth].IsString) checks.Add(new CollectionObservation { depth = depth, count = Count(current), version = Version(current), entry = Hold(Get(current, path[depth])) });
                    if (depth < path.Count - 1) { current = Project(Get(current, path[depth])); chain.Add(current); }
                }
                var leaf = path[path.Count - 1]; var before = Project(Get(current, leaf)); var declared = DeclaredType(current, leaf);
                if (operation == "read")
                {
                    var value = Describe(before, declared, root, path);
                    var observation = Guid.NewGuid().ToString("N");
                    observations[observation] = new Observation { root = root, path = path.ToString(), chain = chain.Select(Hold).ToList(), collections = checks, declaredType = declared };
                    value["snapshotId"] = observation; return value;
                }
                if (operation != "write") throw new InvalidOperationException("unknown-operation");
                ValidateObservation(args["snapshotId"].Value, root, path, chain, checks, declared);
                Writable(current, leaf);
                for (int i = chain.Count - 1; i > 0 && MustWriteBack(Unhold(chain[i])); --i) Writable(Unhold(chain[i - 1]), path[i - 1]);
                var actualType = declared == typeof(object) || declared == typeof(Il2CppSystem.Object) ? before == null ? declared : before.GetType() : declared;
                var desired = ConvertValue(args["value"], actualType);
                var mode = args["operation"].Value;
                if (mode == "add") desired = ConvertValue(new JSONObject { ["text"] = (Convert.ToDecimal(before, CultureInfo.InvariantCulture) + Convert.ToDecimal(desired, CultureInfo.InvariantCulture)).ToString(CultureInfo.InvariantCulture) }, actualType);
                else if (!String.IsNullOrEmpty(mode) && mode != "set") throw new InvalidOperationException("unknown-write-operation");
                started = true;
                Set(current, leaf, desired);
                for (int i = chain.Count - 1; i > 0 && MustWriteBack(current); --i) { var parent = Unhold(chain[i - 1]); Set(parent, path[i - 1], current); current = parent; }
                var result = new JSONObject { ["before"] = Describe(before, declared), ["target"] = Describe(desired, declared) };
                var after = Project(Resolve(Target(root), path));
                result["after"] = Describe(after, declared); result["state"] = SameDescriptor(result["target"], result["after"]) ? "verified" : "executed-unverified";
                observations.Remove(args["snapshotId"].Value);
                return result;
            }
            catch (Exception error)
            {
                var result = new JSONObject { ["state"] = started ? "executed-unverified" : "not-executed", ["error"] = (error.InnerException ?? error).Message };
                string reason;
                if (args != null && StaleReasons.TryGetValue(args["objectId"].Value ?? "", out reason)) { result["reason"] = reason; StaleReasons.Remove(args["objectId"].Value); }
                return result;
            }
        }
        private static object Hold(object value) { return value; }
        private static object Unhold(object value) { return value; }
        private static bool MustWriteBack(object value) { return value != null && (value.GetType().IsValueType || value.GetType().Namespace == "Il2CppInterop.Runtime.InteropTypes.Arrays"); }
        private void ReleaseTree(string root)
        {
            var removed = new HashSet<string>(StringComparer.Ordinal) { root };
            bool changed;
            do
            {
                changed = false;
                foreach (var pair in references)
                    if (!String.IsNullOrEmpty(pair.Value.parent) && removed.Contains(pair.Value.parent) && removed.Add(pair.Key)) changed = true;
            } while (changed);
            foreach (var id in removed) references.Remove(id);
            foreach (var id in methods.Where(pair => removed.Contains(pair.Value.root)).Select(pair => pair.Key).ToList()) methods.Remove(id);
            foreach (var id in observations.Where(pair => removed.Contains(pair.Value.root)).Select(pair => pair.Key).ToList()) observations.Remove(id);
        }
        private void ValidateObservation(string id, string root, JSONArray path, List<object> chain, List<CollectionObservation> checks, Type declared)
        {
            Observation previous;
            if (String.IsNullOrEmpty(id) || !observations.TryGetValue(id, out previous) || previous.root != root || previous.path != path.ToString() || previous.declaredType != declared)
                throw new InvalidOperationException("read-snapshot-required");
            for (int i = 0; i < chain.Count; ++i)
            {
                var a = Unhold(previous.chain[i]); var b = Unhold(chain[i]);
                if (a == null || b == null) throw new InvalidOperationException("stale-reference");
                if (!a.GetType().IsValueType && a.GetType().Namespace != "Il2CppInterop.Runtime.InteropTypes.Arrays" && !Same(a, b)) throw new InvalidOperationException("stale-reference");
                if (a.GetType().IsValueType && !Equals(a, b)) throw new InvalidOperationException("stale-parent-value");
            }
            for (int i = 0; i < checks.Count; ++i)
            {
                var a = previous.collections[i]; var b = checks[i];
                if (a.count != b.count || !Equals(a.version, b.version) || !Same(Unhold(a.entry), Unhold(b.entry))) throw new InvalidOperationException("stale-collection");
            }
        }
        private static Type[] Types(Assembly assembly, string query)
        {
            if (!String.IsNullOrEmpty(query)) { var exact = assembly.GetType(query.Split(',')[0].Trim(), false); if (exact != null) return new[] { exact }; }
            try { return assembly.GetTypes(); }
            catch (ReflectionTypeLoadException error) { return error.Types.Where(type => type != null).ToArray(); }
            catch { return new Type[0]; }
        }
        private static bool GameType(Type type)
        {
            var name = type.FullName ?? ""; var assembly = type.Assembly.GetName().Name;
            return !name.StartsWith("System.") && !name.StartsWith("Il2CppSystem.") && !name.StartsWith("UnityEngine.")
                && !name.StartsWith("Il2CppInterop.") && !name.StartsWith("BepInEx.") && !name.StartsWith("SimpleJSON.") && assembly != "UnityToolsBridge-il2cpp";
        }
        private static JSONNode FindResult(JSONArray rows, JSONArray unsupported)
        {
            return new JSONObject { ["candidates"] = rows, ["unsupported"] = unsupported, ["ambiguous"] = rows.Count > 1 };
        }

        internal IEnumerable<JSONNode> FindFrames(JSONNode args)
        {
            var query = args["type"].Value;
            var name = args["name"].Value;
            var rows = new JSONArray();
            var unsupported = new JSONArray();
            var seen = new List<object>();
            foreach (var assembly in AppDomain.CurrentDomain.GetAssemblies())
            {
                foreach (var type in Types(assembly, query))
                {
                    if (!typeof(UnityEngine.Object).IsAssignableFrom(type))
                    {
                        if (GameType(type) && !String.IsNullOrEmpty(query)
                            && (type.FullName == query || type.Name == query || type.AssemblyQualifiedName == query))
                        {
                            try
                            {
                                rows.Add(new JSONObject { ["objectId"] = Register(type), ["type"] = type.AssemblyQualifiedName,
                                    ["source"] = "static-type", ["path"] = type.FullName });
                            }
                            catch (Exception error) { unsupported.Add(new JSONObject { ["type"] = type.FullName, ["reason"] = (error.InnerException ?? error).Message }); }
                            yield return FindResult(rows, unsupported);
                        }
                        continue;
                    }
                    if (type == typeof(UnityEngine.Object)) continue;
                    if (!String.IsNullOrEmpty(query) && type.FullName != query && type.Name != query && type.AssemblyQualifiedName != query) continue;
                    if (String.IsNullOrEmpty(query) && type != typeof(Component) && type != typeof(GameObject)) continue;
                    var found = new List<object>();
                    try
                    {
                        var nativeType = Il2CppType.From(type, false);
                        if (nativeType != null) foreach (var item in Resources.FindObjectsOfTypeAll(nativeType)) found.Add(item);
                    }
                    catch (Exception error) { unsupported.Add(new JSONObject { ["type"] = type.FullName, ["reason"] = (error.InnerException ?? error).Message }); }
                    foreach (var item in found)
                    {
                        try
                        {
                            if (!Alive(item)) continue;
                            var actual = Project(item);
                            var gameObject = GameObjectOf(actual);
                            if (gameObject == null || !gameObject.scene.isLoaded || (!String.IsNullOrEmpty(name) && gameObject.name.IndexOf(name, StringComparison.OrdinalIgnoreCase) < 0)) continue;
                            if (seen.Any(previous => Same(previous, actual))) continue;
                            seen.Add(actual);
                            var native = item as Il2CppSystem.Object;
                            rows.Add(new JSONObject { ["objectId"] = Register(actual), ["type"] = actual.GetType().AssemblyQualifiedName,
                                ["nativeType"] = native == null ? "" : native.GetIl2CppType().FullName, ["name"] = gameObject.name,
                                ["scene"] = gameObject.scene.path, ["path"] = Hierarchy(gameObject) });
                        }
                        catch (Exception error) { unsupported.Add(new JSONObject { ["type"] = type.FullName, ["reason"] = (error.InnerException ?? error).Message }); }
                        yield return FindResult(rows, unsupported);
                    }
                }
            }
            yield return FindResult(rows, unsupported);
        }

        private JSONNode Find(JSONNode args)
        {
            JSONNode result = null;
            foreach (var frame in FindFrames(args)) result = frame;
            return result ?? new JSONObject { ["candidates"] = new JSONArray(), ["unsupported"] = new JSONArray(), ["ambiguous"] = false };
        }

        private static bool SafeMember(MemberInfo member)
        {
            var field = member as FieldInfo; var property = member as PropertyInfo;
            var type = field != null ? field.FieldType : property != null ? property.PropertyType : null;
            if (member.Name.StartsWith("Native") || member.Name == "Pointer" || member.Name == "ObjectClass" || member.Name == "WasCollected") return false;
            return type == null || SafeType(type);
        }
        internal IEnumerable<JSONNode> InspectFrames(JSONNode args)
        {
            if (args == null || !args.IsObject) throw new InvalidOperationException("arguments-must-be-object");
            var root = args["objectId"].Value;
            var target = Target(root);
            var type = RuntimeType(target);
            var rows = new JSONArray(); var calls = new JSONArray();
            var result = new JSONObject { ["objectId"] = root, ["type"] = type.AssemblyQualifiedName, ["members"] = rows, ["methods"] = calls };
            var work = 0;
            foreach (var member in type.GetMembers(Flags))
            {
                if (++work >= 32) { work = 0; Target(root); yield return null; }
                if (!SafeMember(member)) continue;
                var field = member as FieldInfo; var property = member as PropertyInfo;
                if (target is Type && (field != null ? !field.IsStatic : property != null ? !((property.GetGetMethod(true) ?? property.GetSetMethod(true)).IsStatic) : member is MethodInfo && !((MethodInfo)member).IsStatic)) continue;
                if ((field != null && (field.IsPublic && GameType(field.DeclaringType) || type.IsValueType))
                    || (property != null && property.GetIndexParameters().Length == 0 && (GameType(property.DeclaringType) || typeof(Il2CppObjectBase).IsAssignableFrom(property.DeclaringType))))
                {
                    var declared = field != null ? field.FieldType : property.PropertyType;
                    rows.Add(new JSONObject { ["name"] = member.Name, ["kind"] = field != null ? "field" : "property", ["type"] = declared.AssemblyQualifiedName,
                        ["projection"] = typeof(Il2CppObjectBase).IsAssignableFrom(type) ? "Il2CppInterop" : "managed-data", ["readable"] = field != null || property.CanRead,
                        ["writable"] = field != null ? !field.IsInitOnly && !field.IsLiteral : property.CanWrite });
                }
                var method = member as MethodInfo;
                if (method == null || !GameType(method.DeclaringType) || method.IsSpecialName || method.ContainsGenericParameters || !SafeType(method.ReturnType) || method.GetParameters().Any(p => p.IsOut || !SafeType(p.ParameterType))) continue;
                var id = Guid.NewGuid().ToString("N"); methods[id] = new MethodBinding { root = root, method = method };
                var parameters = new JSONArray(); foreach (var parameter in method.GetParameters()) parameters.Add(parameter.ParameterType.AssemblyQualifiedName);
                calls.Add(new JSONObject { ["name"] = method.Name, ["methodId"] = id, ["declaringType"] = method.DeclaringType.AssemblyQualifiedName,
                    ["static"] = method.IsStatic, ["parameters"] = parameters, ["supported"] = true });
            }
            var indexer = Indexer(type);
            if (indexer != null)
            {
                var count = Count(target); var version = Version(target);
                var entries = new JSONArray(); result["entries"] = entries;
                var dictionary = type.GetProperty("Keys") != null && type.GetMethods().Any(m => m.Name == "ContainsKey");
                if (dictionary)
                {
                    foreach (var key in Enumerate(type.GetProperty("Keys").GetValue(target)))
                    {
                        entries.Add(new JSONObject { ["key"] = Describe(key, key.GetType()), ["value"] = Describe(indexer.GetValue(target, new[] { key }), indexer.PropertyType) });
                        if (++work >= 32) { work = 0; ValidateInspection(root, target, count, version); yield return null; }
                    }
                }
                else for (int i = 0; i < count; ++i)
                {
                    entries.Add(new JSONObject { ["index"] = i, ["value"] = Describe(indexer.GetValue(target, new object[] { i }), indexer.PropertyType) });
                    if (++work >= 32) { work = 0; ValidateInspection(root, target, count, version); yield return null; }
                }
                ValidateInspection(root, target, count, version);
            }
            yield return result;
        }
        private void ValidateInspection(string root, object target, int count, object version)
        {
            if (!Same(Target(root), target) || Count(target) != count || !Equals(Version(target), version)) throw new InvalidOperationException("stale-collection");
        }
        private JSONObject Inspect(object target, string root)
        {
            JSONNode final = null;
            foreach (var frame in InspectFrames(new JSONObject { ["objectId"] = root })) if (frame != null) final = frame;
            return (JSONObject)final;
        }
        private static bool SafeType(Type type)
        {
            if (type == null || type.IsPointer || type.IsByRef || type == typeof(IntPtr) || type == typeof(UIntPtr)) return false;
            if (typeof(MemberInfo).IsAssignableFrom(type) || typeof(Assembly).IsAssignableFrom(type) || typeof(Delegate).IsAssignableFrom(type)) return false;
            if (type.IsArray) return SafeType(type.GetElementType());
            return !type.IsGenericType || type.GetGenericArguments().All(SafeType);
        }
        private static IEnumerable<object> Enumerate(object sequence)
        {
            var get = sequence.GetType().GetMethod("GetEnumerator", Type.EmptyTypes);
            var enumerator = get.Invoke(sequence, null); var move = enumerator.GetType().GetMethod("MoveNext"); var current = enumerator.GetType().GetProperty("Current");
            while ((bool)move.Invoke(enumerator, null)) yield return current.GetValue(enumerator);
        }
        private JSONNode Invoke(object target, string root, JSONNode args, ref bool started)
        {
            MethodBinding binding;
            if (!methods.TryGetValue(args["methodId"].Value ?? "", out binding) || binding.root != root) throw new InvalidOperationException("method-inspection-required");
            var parameters = binding.method.GetParameters(); var types = args["parameterTypes"].AsArray; var values = args["arguments"].AsArray;
            if (types == null || values == null || parameters.Length != types.Count || types.Count != values.Count) throw new InvalidOperationException("method-signature-required");
            var converted = new object[parameters.Length];
            for (int i = 0; i < converted.Length; ++i) { if (types[i].Value != parameters[i].ParameterType.AssemblyQualifiedName) throw new InvalidOperationException("method-signature-mismatch"); converted[i] = ConvertValue(values[i], parameters[i].ParameterType); }
            started = true;
            var value = binding.method.Invoke(binding.method.IsStatic ? null : target, converted);
            return new JSONObject { ["state"] = "executed", ["value"] = Describe(value, binding.method.ReturnType) };
        }
        private static JSONArray Path(JSONNode args)
        {
            JSONArray path;
            if (args["path"] != null) path = args["path"].AsArray;
            else { path = new JSONArray(); if (!String.IsNullOrEmpty(args["member"].Value)) path.Add(args["member"].Value); }
            if (path == null || path.Count == 0) throw new InvalidOperationException("member-path-required"); return path;
        }
        private object Resolve(object root, JSONArray path) { var current = root; foreach (var step in path.Children) current = Project(Get(current, step)); return current; }
        private static PropertyInfo Indexer(Type type) { return type.GetProperties(Flags).FirstOrDefault(p => p.Name == "Item" && p.GetIndexParameters().Length == 1); }
        private static int Count(object value)
        {
            var count = value.GetType().GetProperty("Count") ?? value.GetType().GetProperty("Length");
            if (count == null) throw new InvalidOperationException("unsupported-collection"); return Convert.ToInt32(count.GetValue(value), CultureInfo.InvariantCulture);
        }
        private static object Version(object value)
        {
            var property = value.GetType().GetProperty("_version", Flags) ?? value.GetType().GetProperty("version", Flags);
            var field = value.GetType().GetField("_version", Flags);
            return property != null ? property.GetValue(value) : field == null ? null : field.GetValue(value);
        }
        private object Key(object parent, JSONNode step, PropertyInfo indexer)
        {
            var type = indexer.GetIndexParameters()[0].ParameterType;
            if (step["key"] != null) return ConvertValue(step["key"].IsObject ? step["key"] : new JSONObject { ["text"] = step["key"].Value }, type);
            if (step["index"] == null || !Int32.TryParse(step["index"].Value, out var index) || index < 0 || index >= Count(parent)) throw new InvalidOperationException("index-out-of-range");
            return index;
        }
        private object Get(object parent, JSONNode step)
        {
            if (!Alive(parent)) throw new InvalidOperationException("stale-reference");
            if (step.IsString) { var member = Member(RuntimeType(parent), step.Value); var field = member as FieldInfo; return field != null ? field.GetValue(Instance(parent)) : ((PropertyInfo)member).GetValue(Instance(parent)); }
            var indexer = Indexer(parent.GetType()); if (indexer == null) throw new InvalidOperationException("unsupported-collection");
            var key = Key(parent, step, indexer);
            if (step["key"] != null) { var contains = parent.GetType().GetMethod("ContainsKey", new[] { key.GetType() }); if (contains == null || !(bool)contains.Invoke(parent, new[] { key })) throw new InvalidOperationException("missing-key"); }
            return indexer.GetValue(parent, new[] { key });
        }
        private void Set(object parent, JSONNode step, object value)
        {
            Writable(parent, step);
            if (step.IsString) { var member = Member(RuntimeType(parent), step.Value); var field = member as FieldInfo; if (field != null) field.SetValue(Instance(parent), value); else ((PropertyInfo)member).SetValue(Instance(parent), value); return; }
            var indexer = Indexer(parent.GetType()); Get(parent, step); indexer.SetValue(parent, value, new[] { Key(parent, step, indexer) });
        }
        private static void Writable(object parent, JSONNode step)
        {
            var member = step.IsString ? Member(RuntimeType(parent), step.Value) : Indexer(parent.GetType());
            var field = member as FieldInfo; var property = member as PropertyInfo;
            if (field != null ? field.IsInitOnly || field.IsLiteral : property == null || !property.CanWrite) throw new InvalidOperationException("readonly-member");
        }
        private static Type DeclaredType(object parent, JSONNode step)
        {
            var member = step.IsString ? Member(RuntimeType(parent), step.Value) : Indexer(parent.GetType());
            var field = member as FieldInfo; return field != null ? field.FieldType : ((PropertyInfo)member).PropertyType;
        }
        private static MemberInfo Member(Type type, string name)
        {
            for (var current = type; current != null; current = current.BaseType)
            {
                var property = current.GetProperty(name, Flags | BindingFlags.DeclaredOnly);
                if (property != null && property.GetIndexParameters().Length == 0 && SafeMember(property) && !current.FullName.StartsWith("Il2CppInterop.")) return property;
                var field = current.GetField(name, Flags | BindingFlags.DeclaredOnly);
                if (field != null && SafeMember(field) && (current.IsValueType || GameType(current))) return field;
            }
            throw new MissingMemberException(type.FullName, name);
        }
        private object ConvertValue(JSONNode value, Type type)
        {
            if (value["objectId"] != null) { var target = Target(value["objectId"].Value); if (!type.IsInstanceOfType(target)) throw new InvalidCastException("object-type-mismatch"); return target; }
            if (value["null"].AsBool) { if (type.IsValueType && Nullable.GetUnderlyingType(type) == null) throw new InvalidCastException("null-for-value-type"); return null; }
            type = Nullable.GetUnderlyingType(type) ?? type;
            var text = value["text"].Value;
            if (type == typeof(string)) return text;
            if (type.IsEnum) return Enum.Parse(type, text, false);
            if (type == typeof(decimal)) return NumericValue.ParseDecimalExact(text);
            if (type.IsPrimitive) return Convert.ChangeType(text, type, CultureInfo.InvariantCulture);
            throw new InvalidCastException("unsupported-interop-value-type");
        }
        private static void AddNumericMetadata(JSONObject result, Type type)
        {
            var actual = type.IsEnum ? Enum.GetUnderlyingType(type) : type;
            if (actual == typeof(byte) || actual == typeof(sbyte) || actual == typeof(short) || actual == typeof(ushort)
                || actual == typeof(int) || actual == typeof(uint) || actual == typeof(long) || actual == typeof(ulong))
            {
                result["bits"] = actual == typeof(byte) || actual == typeof(sbyte) ? 8 : actual == typeof(short) || actual == typeof(ushort) ? 16 : actual == typeof(int) || actual == typeof(uint) ? 32 : 64;
                result["signed"] = actual == typeof(sbyte) || actual == typeof(short) || actual == typeof(int) || actual == typeof(long);
            }
            else if (actual == typeof(decimal)) { result["bits"] = 128; result["signed"] = true; }
            if (type.IsEnum) result["enum"] = true;
        }
        private static bool SameDescriptor(JSONNode expected, JSONNode actual)
        {
            foreach (var field in new[] { "type", "projectionType", "bits", "signed", "enum", "null", "text", "objectId" })
                if (expected[field].ToString() != actual[field].ToString()) return false;
            return true;
        }
        private JSONObject Describe(object value, Type declared, string root = null, JSONArray path = null)
        {
            value = Project(value);
            if (value == null) return new JSONObject { ["type"] = declared.AssemblyQualifiedName, ["null"] = true };
            var type = value.GetType(); var result = new JSONObject { ["type"] = declared.AssemblyQualifiedName, ["projectionType"] = type.AssemblyQualifiedName, ["null"] = false };
            if (type.IsPrimitive || type.IsEnum || value is string || value is decimal)
            {
                result["text"] = Convert.ToString(value, CultureInfo.InvariantCulture);
                AddNumericMetadata(result, type);
                var actual = type.IsEnum ? Enum.GetUnderlyingType(type) : type;
                if (actual == typeof(byte) || actual == typeof(sbyte) || actual == typeof(short) || actual == typeof(ushort) || actual == typeof(int) || actual == typeof(uint) || actual == typeof(long) || actual == typeof(ulong))
                {
                    result["bits"] = actual == typeof(byte) || actual == typeof(sbyte) ? 8 : actual == typeof(short) || actual == typeof(ushort) ? 16 : actual == typeof(int) || actual == typeof(uint) ? 32 : 64;
                    result["signed"] = actual == typeof(sbyte) || actual == typeof(short) || actual == typeof(int) || actual == typeof(long);
                }
                if (type.IsEnum) result["enum"] = true;
            }
            else { result["text"] = type.FullName; if (!type.IsValueType) result["objectId"] = Register(value, root, path); }
            return result;
        }
    }
}
