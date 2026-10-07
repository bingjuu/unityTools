using System;
using System.Collections;
using System.Collections.Generic;
using System.Globalization;
using System.Linq;
using System.Reflection;
using SimpleJSON;

namespace UnityTools
{
    public sealed class ObjectAccess
    {
        private sealed class MethodBinding
        {
            public string objectId;
            public MethodInfo method;
        }

        private sealed class CollectionCheck
        {
            public object collection;
            public bool dictionary;
            public object key;
            public int index;
            public object value;
        }

        private sealed class PathSnapshot
        {
            public string rootId;
            public JSONArray path;
            public List<object> chain;
            public List<CollectionCheck> collections;
        }

        private sealed class ObjectRecord
        {
            public WeakReference value;
            public string rootId;
            public JSONArray path;
        }

        private readonly Dictionary<string, ObjectRecord> objects = new Dictionary<string, ObjectRecord>();
        private readonly Dictionary<string, MethodBinding> methods = new Dictionary<string, MethodBinding>();
        private readonly Dictionary<string, PathSnapshot> snapshots = new Dictionary<string, PathSnapshot>();
        public Func<object, bool> IsAlive = value => true;
        public Func<object, object> ResolveActual = value => value;
        public Action<object, string> ObjectObserved;

        public string Register(object value)
        {
            if (value == null) return null;
            foreach (var entry in objects)
            {
                var existing = entry.Value.value.Target;
                if (existing != null && ReferenceEquals(existing, value)) { NotifyObserved(value, entry.Key); return entry.Key; }
            }
            var id = Guid.NewGuid().ToString("N");
            objects[id] = new ObjectRecord { value = new WeakReference(value) };
            NotifyObserved(value, id);
            return id;
        }

        private void NotifyObserved(object value, string id)
        {
            var observed = ObjectObserved;
            if (observed == null) return;
            try { observed(value, id); } catch { }
        }

        public string RegisterType(Type type)
        {
            if (!IsGameType(type)) throw new InvalidOperationException("game-type-required");
            return Register(type);
        }

        private static Type RuntimeType(object target) { return target as Type ?? target.GetType(); }
        private static object Instance(object target) { return target is Type ? null : target; }

        public void Clear()
        {
            objects.Clear();
            methods.Clear();
            snapshots.Clear();
        }

        public object ResolveForBridge(string id)
        {
            try { return Target(id); }
            catch { return null; }
        }

        public JSONNode Describe(object value, Type declared = null)
        {
            if (value == null)
            {
                var nil = new JSONObject();
                nil["type"] = declared == null ? "null" : declared.AssemblyQualifiedName;
                nil["null"] = true;
                return nil;
            }
            value = ResolveActual(value);
            var type = value.GetType();
            var result = new JSONObject();
            result["type"] = (declared ?? type).AssemblyQualifiedName;
            result["projectionType"] = type.AssemblyQualifiedName;
            result["null"] = false;
            if (type.IsPrimitive || type.IsEnum || value is string || value is decimal)
            {
                result["text"] = Convert.ToString(value, CultureInfo.InvariantCulture);
                AddNumericMetadata(result, type);
                if (type.IsEnum) result["enum"] = true;
            }
            else
            {
                result["objectId"] = Register(value);
                result["text"] = type.FullName;
            }
            return result;
        }

        private void Release(string rootId)
        {
            var removed = new HashSet<string>(StringComparer.Ordinal) { rootId };
            bool changed;
            do
            {
                changed = false;
                foreach (var pair in objects)
                    if (pair.Value.rootId != null && removed.Contains(pair.Value.rootId) && removed.Add(pair.Key)) changed = true;
            } while (changed);
            foreach (var id in removed) objects.Remove(id);
            foreach (var id in methods.Where(pair => removed.Contains(pair.Value.objectId)).Select(pair => pair.Key).ToList()) methods.Remove(id);
            foreach (var id in snapshots.Where(pair => removed.Contains(pair.Value.rootId)).Select(pair => pair.Key).ToList()) snapshots.Remove(id);
        }

        private static bool SafeType(Type type)
        {
            if (type == null || type.IsPointer || type.IsByRef || type == typeof(IntPtr) || type == typeof(UIntPtr)) return false;
            if (typeof(MemberInfo).IsAssignableFrom(type) || typeof(Assembly).IsAssignableFrom(type) || typeof(Delegate).IsAssignableFrom(type)) return false;
            if (type.IsArray) return SafeType(type.GetElementType());
            return !type.IsGenericType || type.GetGenericArguments().All(SafeType);
        }

        private static bool SafeMember(MemberInfo member)
        {
            var field = member as FieldInfo;
            var property = member as PropertyInfo;
            if (member.Name.StartsWith("Native", StringComparison.Ordinal) && member.Name.Contains("Ptr")) return false;
            return field != null ? SafeType(field.FieldType) : property != null && SafeType(property.PropertyType);
        }

        private object Target(string id)
        {
            ObjectRecord record;
            object value;
            if (String.IsNullOrEmpty(id) || !objects.TryGetValue(id, out record)
                || (value = record.value.Target) == null || !IsAlive(value))
                throw new InvalidOperationException("stale-reference");
            if (!String.IsNullOrEmpty(record.rootId))
            {
                var current = Target(record.rootId);
                foreach (var step in record.path) current = ResolveActual(Get(current, step));
                if (!SameObject(current, value)) throw new InvalidOperationException("stale-reference");
            }
            return ResolveActual(value);
        }

        private const BindingFlags Flags = BindingFlags.Instance | BindingFlags.Static | BindingFlags.Public | BindingFlags.NonPublic;

        private static MemberInfo Member(Type type, string name)
        {
            for (var current = type; current != null; current = current.BaseType)
            {
                var field = current.GetField(name, Flags | BindingFlags.DeclaredOnly);
                if (field != null && SafeMember(field)) return field;
                var property = current.GetProperty(name, Flags | BindingFlags.DeclaredOnly);
                if (property != null && property.GetIndexParameters().Length == 0 && SafeMember(property)) return property;
            }
            throw new MissingMemberException(type.FullName, name);
        }

        private static Type ValueType(object parent, JSONNode step)
        {
            if (step.IsString)
            {
                var member = Member(RuntimeType(parent), step.Value);
                return member is FieldInfo ? ((FieldInfo)member).FieldType : ((PropertyInfo)member).PropertyType;
            }
            var type = parent.GetType();
            if (type.IsArray) return type.GetElementType();
            var dictionary = GenericInterface(type, typeof(IDictionary<,>));
            if (dictionary != null) return dictionary.GetGenericArguments()[1];
            var list = GenericInterface(type, typeof(IList<>));
            return list == null ? typeof(object) : list.GetGenericArguments()[0];
        }

        private static Type GenericInterface(Type type, Type definition)
        {
            return type.GetInterfaces().Concat(new[] { type }).FirstOrDefault(t =>
                t.IsGenericType && t.GetGenericTypeDefinition() == definition);
        }

        private static object Key(object parent, JSONNode step)
        {
            var dictionary = GenericInterface(parent.GetType(), typeof(IDictionary<,>));
            return dictionary == null ? (object)step["key"].Value
                : ConvertValueText(step["key"].Value, dictionary.GetGenericArguments()[0]);
        }

        private static object Get(object parent, JSONNode step)
        {
            if (parent == null) throw new InvalidOperationException("null-parent");
            if (step.IsString)
            {
                var member = Member(RuntimeType(parent), step.Value);
                if (member is FieldInfo) return ((FieldInfo)member).GetValue(Instance(parent));
                var property = (PropertyInfo)member;
                if (!property.CanRead) throw new InvalidOperationException("unreadable-member");
                return property.GetValue(Instance(parent), null);
            }
            if (step["key"] != null)
            {
                var dictionary = parent as IDictionary;
                if (dictionary == null) throw new InvalidOperationException("unsupported-dictionary");
                var key = Key(parent, step);
                if (!dictionary.Contains(key)) throw new InvalidOperationException("missing-key");
                return dictionary[key];
            }
            var list = parent as IList;
            if (list == null) throw new InvalidOperationException("unsupported-list");
            var index = step["index"].AsInt;
            if (index < 0 || index >= list.Count) throw new InvalidOperationException("index-out-of-range");
            return list[index];
        }

        private static void EnsureWritable(object parent, JSONNode step)
        {
            if (!step.IsString) return;
            var member = Member(RuntimeType(parent), step.Value);
            if (member is FieldInfo)
            {
                var field = (FieldInfo)member;
                if (field.IsInitOnly || field.IsLiteral) throw new InvalidOperationException("readonly-member");
                return;
            }
            if (!((PropertyInfo)member).CanWrite) throw new InvalidOperationException("readonly-member");
        }

        private static void Set(object parent, JSONNode step, object value)
        {
            if (step.IsString)
            {
                EnsureWritable(parent, step);
                var member = Member(RuntimeType(parent), step.Value);
                if (member is FieldInfo) ((FieldInfo)member).SetValue(Instance(parent), value);
                else ((PropertyInfo)member).SetValue(Instance(parent), value, null);
            }
            else if (step["key"] != null)
            {
                var dictionary = parent as IDictionary;
                if (dictionary == null) throw new InvalidOperationException("unsupported-dictionary");
                var key = Key(parent, step);
                if (!dictionary.Contains(key)) throw new InvalidOperationException("missing-key");
                dictionary[key] = value;
            }
            else
            {
                var list = parent as IList;
                if (list == null) throw new InvalidOperationException("unsupported-list");
                var index = step["index"].AsInt;
                if (index < 0 || index >= list.Count) throw new InvalidOperationException("index-out-of-range");
                list[index] = value;
            }
        }

        private object ConvertValue(JSONNode value, Type target)
        {
            if (value == null) throw new InvalidCastException("value-required");
            if (value["objectId"] != null)
            {
                var referenced = Target(value["objectId"].Value);
                if (!target.IsInstanceOfType(referenced)) throw new InvalidCastException("object-type-mismatch");
                return referenced;
            }
            if (value["null"].AsBool)
            {
                if (target.IsValueType && Nullable.GetUnderlyingType(target) == null)
                    throw new InvalidCastException("null-for-value-type");
                return null;
            }
            target = Nullable.GetUnderlyingType(target) ?? target;
            var text = value["text"].Value;
            if (target == typeof(string)) return text;
            if (target.IsEnum) return Enum.Parse(target, text, false);
            if (target == typeof(decimal)) return NumericValue.ParseDecimalExact(text);
            if (!target.IsPrimitive) throw new InvalidCastException("unsupported-value-type");
            return ConvertValueText(text, target);
        }

        private static object ConvertValueText(string text, Type target)
        {
            if (target == typeof(decimal)) return NumericValue.ParseDecimalExact(text);
            return Convert.ChangeType(text, target, CultureInfo.InvariantCulture);
        }

        private static decimal ParseDecimalExact(string text)
        {
            if (String.IsNullOrWhiteSpace(text)) throw new FormatException("invalid-decimal");
            var raw = text.Trim();
            var mantissa = raw;
            var exponent = 0;
            var exponentIndex = raw.IndexOfAny(new[] { 'e', 'E' });
            if (exponentIndex >= 0)
            {
                mantissa = raw.Substring(0, exponentIndex);
                if (!Int32.TryParse(raw.Substring(exponentIndex + 1), NumberStyles.AllowLeadingSign, CultureInfo.InvariantCulture, out exponent))
                    throw new FormatException("invalid-decimal");
            }
            if (mantissa.StartsWith("+") || mantissa.StartsWith("-")) mantissa = mantissa.Substring(1);
            var dot = mantissa.IndexOf('.');
            var fractional = dot < 0 ? 0 : mantissa.Length - dot - 1;
            var digits = dot < 0 ? mantissa : mantissa.Remove(dot, 1);
            if (digits.Length == 0 || digits.Any(c => c < '0' || c > '9')) throw new FormatException("invalid-decimal");
            digits = digits.TrimStart('0');
            if (digits.Length == 0) return 0m;
            var scale = fractional - exponent;
            while (scale > 0 && digits.EndsWith("0", StringComparison.Ordinal)) { digits = digits.Substring(0, digits.Length - 1); --scale; }
            if (scale < 0) { digits += new string('0', -scale); scale = 0; }
            if (scale > 28 || digits.Length > 29) throw new OverflowException("decimal-precision-loss");
            decimal result;
            if (!Decimal.TryParse(raw, NumberStyles.Float, CultureInfo.InvariantCulture, out result))
                throw new OverflowException("decimal-overflow");
            return result;
        }

        private static void AddNumericMetadata(JSONObject result, Type type)
        {
            var actual = type.IsEnum ? Enum.GetUnderlyingType(type) : type;
            if (actual == typeof(byte) || actual == typeof(sbyte) || actual == typeof(short)
                || actual == typeof(ushort) || actual == typeof(int) || actual == typeof(uint)
                || actual == typeof(long) || actual == typeof(ulong))
            {
                result["bits"] = actual == typeof(byte) || actual == typeof(sbyte) ? 8
                    : actual == typeof(short) || actual == typeof(ushort) ? 16
                    : actual == typeof(int) || actual == typeof(uint) ? 32 : 64;
                result["signed"] = actual == typeof(sbyte) || actual == typeof(short)
                    || actual == typeof(int) || actual == typeof(long);
            }
            else if (actual == typeof(decimal))
            {
                result["bits"] = 128;
                result["signed"] = true;
            }
        }

        private static JSONArray Path(JSONNode args)
        {
            if (args["path"] != null) return args["path"].AsArray;
            var result = new JSONArray();
            result.Add(args["member"].Value);
            return result;
        }

        private static bool IsGameType(Type type)
        {
            var fullName = type == null ? "" : type.FullName ?? "";
            return !(fullName.StartsWith("System.", StringComparison.Ordinal)
                || fullName.StartsWith("UnityEngine.", StringComparison.Ordinal)
                || fullName.StartsWith("BepInEx.", StringComparison.Ordinal)
                || fullName.StartsWith("Il2Cpp", StringComparison.Ordinal));
        }

        private JSONObject Inspect(object target, string objectId)
        {
            JSONNode result = null;
            foreach (var frame in ExecuteFrames("inspect", new JSONObject { ["objectId"] = objectId }))
                if (frame != null) result = frame;
            return (JSONObject)result;
        }

        public IEnumerable<JSONNode> ExecuteFrames(string operation, JSONNode args)
        {
            if (operation != "inspect")
            {
                yield return ExecuteNode(operation, args);
                yield break;
            }
            if (args == null || !args.IsObject) throw new InvalidOperationException("arguments-must-be-object");
            var objectId = args["objectId"].Value;
            var target = Target(objectId);
            var type = RuntimeType(target);
            var result = new JSONObject();
            var members = new JSONArray();
            var methodRows = new JSONArray();
            result["objectId"] = objectId;
            result["type"] = type.AssemblyQualifiedName;
            var work = 0;
            foreach (var member in type.GetMembers(Flags))
            {
                var field = member as FieldInfo;
                var property = member as PropertyInfo;
                if ((field != null || property != null) && !SafeMember(member)) continue;
                if (target is Type && (field != null ? !field.IsStatic : property != null ? !((property.GetGetMethod(true) ?? property.GetSetMethod(true)).IsStatic) : member is MethodInfo && !((MethodInfo)member).IsStatic)) continue;
                if (field != null || (property != null && property.GetIndexParameters().Length == 0))
                {
                    var item = new JSONObject();
                    item["name"] = member.Name;
                    item["kind"] = field != null ? "field" : "property";
                    item["type"] = (field != null ? field.FieldType : property.PropertyType).AssemblyQualifiedName;
                    item["readable"] = field != null || property.CanRead;
                    item["writable"] = field != null ? !(field.IsLiteral || field.IsInitOnly) : property.CanWrite;
                    members.Add(item);
                }
                var method = member as MethodInfo;
                if (method != null && !method.IsSpecialName && !method.ContainsGenericParameters && IsGameType(method.DeclaringType) && SafeType(method.ReturnType))
                {
                    var supported = true;
                    var parameterTypes = new JSONArray();
                    foreach (var parameter in method.GetParameters())
                    {
                        parameterTypes.Add(parameter.ParameterType.AssemblyQualifiedName);
                        supported &= SafeType(parameter.ParameterType);
                    }
                    if (supported)
                    {
                        var methodId = Guid.NewGuid().ToString("N");
                        methods[methodId] = new MethodBinding { objectId = objectId, method = method };
                        methodRows.Add(new JSONObject { ["methodId"] = methodId, ["name"] = method.Name,
                            ["declaringType"] = method.DeclaringType.AssemblyQualifiedName, ["static"] = method.IsStatic,
                            ["parameters"] = parameterTypes, ["supported"] = true });
                    }
                }
                if (++work >= 32) { work = 0; yield return null; }
            }
            result["members"] = members;
            result["methods"] = methodRows;
            if (target is IDictionary)
            {
                var rows = new JSONArray();
                result["entries"] = rows;
                foreach (DictionaryEntry entry in (IDictionary)target)
                {
                    var row = new JSONObject();
                    row["key"] = Convert.ToString(entry.Key, CultureInfo.InvariantCulture);
                    row["value"] = Describe(entry.Value);
                    rows.Add(row);
                    if (++work >= 32) { work = 0; yield return null; }
                }
            }
            if (target is IList)
            {
                var rows = new JSONArray();
                result["entries"] = rows;
                var list = (IList)target;
                for (var i = 0; i < list.Count; ++i)
                {
                    var row = new JSONObject();
                    row["index"] = i;
                    row["value"] = Describe(list[i]);
                    rows.Add(row);
                    if (++work >= 32) { work = 0; yield return null; }
                }
            }
            yield return result;
        }

        public string Execute(string operation, string body)
        {
            try { return ExecuteNode(operation, JSON.Parse(body)).ToString(); }
            catch (Exception error)
            {
                var result = new JSONObject();
                result["error"] = (error.InnerException ?? error).Message;
                result["state"] = "not-executed";
                return result.ToString();
            }
        }

        public JSONNode ExecuteNode(string operation, JSONNode args)
        {
            var sideEffectStarted = false;
            try { return ExecuteNodeCore(operation, args, ref sideEffectStarted); }
            catch (Exception error)
            {
                var result = new JSONObject();
                result["error"] = (error.InnerException ?? error).Message;
                result["state"] = sideEffectStarted ? "executed-unverified" : "not-executed";
                return result;
            }
        }

        private JSONNode ExecuteNodeCore(string operation, JSONNode args, ref bool sideEffectStarted)
        {
            if (args == null || !args.IsObject) throw new InvalidOperationException("arguments-must-be-object");
            if (operation == "release")
            {
                if (args["all"].AsBool) Clear();
                else
                {
                    if (!String.IsNullOrEmpty(args["objectId"].Value)) Release(args["objectId"].Value);
                    if (args["objectIds"].IsArray) foreach (var id in args["objectIds"].Children) Release(id.Value);
                    if (args["snapshotIds"].IsArray) foreach (var id in args["snapshotIds"].Children) snapshots.Remove(id.Value);
                }
                return new JSONObject { ["state"] = "completed" };
            }
            var objectId = args["objectId"].Value;
            var target = Target(objectId);
            if (operation == "inspect") return Inspect(target, objectId);
            if (operation == "invoke")
            {
                var methodId = args["methodId"].Value;
                MethodBinding binding;
                if (String.IsNullOrEmpty(methodId) || !methods.TryGetValue(methodId, out binding) || binding.objectId != objectId)
                    throw new InvalidOperationException("method-inspection-required");
                var signature = args["parameterTypes"].AsArray;
                var values = args["arguments"].AsArray;
                var parameters = binding.method.GetParameters();
                if (signature == null || values == null || signature.Count != values.Count || signature.Count != parameters.Length)
                    throw new InvalidOperationException("method-signature-required");
                for (var i = 0; i < parameters.Length; ++i)
                {
                    if (signature[i].Value != parameters[i].ParameterType.AssemblyQualifiedName
                        || parameters[i].ParameterType.IsByRef || parameters[i].ParameterType.IsPointer)
                        throw new InvalidOperationException("method-signature-mismatch");
                }
                var converted = new object[parameters.Length];
                for (var i = 0; i < parameters.Length; ++i) converted[i] = ConvertValue(values[i], parameters[i].ParameterType);
                sideEffectStarted = true;
                var result = new JSONObject();
                result["value"] = Describe(binding.method.Invoke(binding.method.IsStatic ? null : target, converted));
                result["state"] = "executed";
                return result;
            }

            var path = Path(args);
            if (path == null || path.Count == 0) throw new InvalidOperationException("member-path-required");
            var parents = new List<object>();
            var chain = new List<object> { target };
            var current = target;
            for (var i = 0; i < path.Count - 1; ++i)
            {
                var step = path[i];
                current = ResolveActual(Get(current, step));
                if (current == null || !IsAlive(current)) throw new InvalidOperationException("stale-reference");
                parents.Add(chain[chain.Count - 1]);
                chain.Add(current);
            }
            var leaf = path[path.Count - 1];
            var before = Get(current, leaf);
            if (operation == "read")
            {
                var result = Describe(before, ValueType(current, leaf));
                if (result["objectId"] != null && before != null && !before.GetType().IsValueType)
                {
                    var childId = Guid.NewGuid().ToString("N");
                    objects[childId] = new ObjectRecord { value = new WeakReference(before), rootId = objectId, path = JSON.Parse(path.ToString()).AsArray };
                    result["objectId"] = childId;
                    NotifyObserved(before, childId);
                }
                var snapshotId = SaveSnapshot(objectId, path, chain, current, leaf);
                result["snapshotId"] = snapshotId;
                return result;
            }
            if (operation != "write") throw new InvalidOperationException("unknown-operation");
            ValidateSnapshot(args["snapshotId"].Value, objectId, path, chain, current, leaf);
            EnsureWritable(current, leaf);
            var copy = current;
            for (var i = path.Count - 2; i >= 0 && copy.GetType().IsValueType; --i)
            {
                EnsureWritable(parents[i], path[i]);
                copy = parents[i];
            }
            var declared = ValueType(current, leaf);
            var type = declared == typeof(object) && before != null ? before.GetType() : declared;
            var value = ConvertValue(args["value"], type);
            if (args["expected"] != null && !Equals(before, ConvertValue(args["expected"], type)))
                throw new InvalidOperationException("stale-value");
            if (args["operation"].Value == "add")
            {
                var sum = Convert.ToDecimal(before, CultureInfo.InvariantCulture) + Convert.ToDecimal(value, CultureInfo.InvariantCulture);
                value = ConvertValue(new JSONObject { ["text"] = sum.ToString(CultureInfo.InvariantCulture) }, type);
            }
            snapshots.Remove(args["snapshotId"].Value ?? "");
            sideEffectStarted = true;
            Set(current, leaf, value);
            for (var i = path.Count - 2; i >= 0 && current.GetType().IsValueType; --i)
            {
                Set(parents[i], path[i], current);
                current = parents[i];
            }
            var resultWrite = new JSONObject();
            resultWrite["before"] = Describe(before, declared);
            resultWrite["target"] = Describe(value, declared);
            try
            {
                current = Target(objectId);
                foreach (var step in path) current = Get(current, step);
                resultWrite["after"] = Describe(current, declared);
                resultWrite["state"] = SameDescriptor(resultWrite["target"], resultWrite["after"]) ? "verified" : "executed-unverified";
            }
            catch (Exception error)
            {
                resultWrite["state"] = "executed-unverified";
                resultWrite["error"] = error.Message;
            }
            return resultWrite;
        }

        private static bool SameDescriptor(JSONNode expected, JSONNode actual)
        {
            foreach (var field in new[] { "type", "projectionType", "bits", "signed", "enum", "null", "text", "objectId" })
                if (expected[field].ToString() != actual[field].ToString()) return false;
            return true;
        }

        private string SaveSnapshot(string rootId, JSONArray path, List<object> chain, object leafParent, JSONNode leaf)
        {
            var snapshotId = Guid.NewGuid().ToString("N");
            var checks = new List<CollectionCheck>();
            var current = chain[0];
            for (var i = 0; i < path.Count; ++i)
            {
                var step = path[i];
                if (!step.IsString) checks.Add(CaptureCollection(current, step));
                if (i < path.Count - 1) current = ResolveActual(Get(current, step));
            }
            snapshots[snapshotId] = new PathSnapshot { rootId = rootId, path = path, chain = chain, collections = checks };
            return snapshotId;
        }

        private static CollectionCheck CaptureCollection(object parent, JSONNode step)
        {
            if (step["key"] != null)
            {
                var dictionary = parent as IDictionary;
                if (dictionary == null) throw new InvalidOperationException("unsupported-dictionary");
                var key = Key(parent, step);
                if (!dictionary.Contains(key)) throw new InvalidOperationException("missing-key");
                return new CollectionCheck { collection = parent, dictionary = true, key = key, value = dictionary[key] };
            }
            var list = parent as IList;
            if (list == null) throw new InvalidOperationException("unsupported-list");
            var index = step["index"].AsInt;
            if (index < 0 || index >= list.Count) throw new InvalidOperationException("index-out-of-range");
            return new CollectionCheck { collection = parent, index = index, value = list[index] };
        }

        private void ValidateSnapshot(string snapshotId, string rootId, JSONArray path, List<object> chain, object leafParent, JSONNode leaf)
        {
            if (String.IsNullOrEmpty(snapshotId))
            {
                if (path.Count > 1 || !leaf.IsString) throw new InvalidOperationException("read-snapshot-required");
                return;
            }
            PathSnapshot snapshot;
            if (!snapshots.TryGetValue(snapshotId, out snapshot) || snapshot.rootId != rootId
                || snapshot.path.ToString() != path.ToString()) throw new InvalidOperationException("stale-reference");
            var root = Target(rootId);
            if (!SameObject(root, snapshot.chain[0])) throw new InvalidOperationException("stale-reference");
            var current = root;
            for (var i = 0; i < path.Count - 1; ++i)
            {
                current = ResolveActual(Get(current, path[i]));
                if (!SameObject(current, snapshot.chain[i + 1])) throw new InvalidOperationException("stale-reference");
            }
            if (!SameObject(current, leafParent)) throw new InvalidOperationException("stale-reference");
            foreach (var check in snapshot.collections)
            {
                if (!SameObject(check.collection, ResolveActual(check.collection))) throw new InvalidOperationException("stale-reference");
                if (check.dictionary)
                {
                    var dictionary = (IDictionary)check.collection;
                    if (!dictionary.Contains(check.key) || !Equals(dictionary[check.key], check.value)) throw new InvalidOperationException("stale-collection");
                }
                else
                {
                    var list = (IList)check.collection;
                    if (check.index < 0 || check.index >= list.Count || !Equals(list[check.index], check.value)) throw new InvalidOperationException("stale-collection");
                }
            }
        }

        private static bool SameObject(object left, object right)
        {
            if (left == null || right == null) return left == right;
            return left.GetType().IsValueType ? Equals(left, right) : ReferenceEquals(left, right);
        }
    }
}
