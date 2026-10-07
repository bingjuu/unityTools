using System;
using SimpleJSON;
using XUnity.AutoTranslator.Plugin.Core.Endpoints;
using XUnity.AutoTranslator.Plugin.Core.Endpoints.Http;
using XUnity.AutoTranslator.Plugin.Core.Extensions;
using XUnity.AutoTranslator.Plugin.Core.Web;

namespace UnityToolsTranslate
{
    public class UnityToolsTranslateEndpoint : HttpEndpoint
    {
        private string endpoint;
        private int concurrency;
        private int batchSize;
        public override string Id => "UnityToolsTranslate";
        public override string FriendlyName => "unityTools LLM Proxy";
        public override int MaxConcurrency => concurrency;
        public override int MaxTranslationsPerRequest => batchSize;
        public override void Initialize(IInitializationContext context)
        {
            endpoint = context.GetOrCreateSetting("Custom", "Url", "http://127.0.0.1:6800").TrimEnd('/') + "/batch";
            concurrency = Math.Max(1, context.GetOrCreateSetting("General", "MaxConcurrentTranslations", 8));
            batchSize = Math.Max(1, context.GetOrCreateSetting("General", "MaxTranslationsPerRequest", 10));
            context.DisableCertificateChecksFor(new Uri(endpoint).Host);
            context.SetTranslationDelay(0.1f);
        }
        public override void OnCreateRequest(IHttpRequestCreationContext context)
        {
            var payload = new JSONObject();
            var texts = new JSONArray();
            foreach (string text in context.UntranslatedTexts) texts.Add(text);
            payload["texts"] = texts;
            payload["from"] = context.SourceLanguage;
            payload["to"] = context.DestinationLanguage;
            var request = new XUnityWebRequest("POST", endpoint, payload.ToString());
            request.Headers.Add("Content-Type", "application/json; charset=utf-8");
            context.Complete(request);
        }
        public override void OnExtractTranslation(IHttpTranslationExtractionContext context)
        {
            try
            {
                string[] result;
                var root = JSON.Parse(context.Response.Data);
                var array = root["translations"];
                if (array == null || !array.IsArray)
                { context.Fail("translations is not an array"); return; }
                result = new string[array.Count];
                for (int i = 0; i < array.Count; ++i)
                {
                    if (!array[i].IsString) { context.Fail("translation element is not a string"); return; }
                    result[i] = array[i].Value;
                }
                if (result.Length != context.UntranslatedTexts.Length) { context.Fail("Batch item count mismatch"); return; }
                context.Complete(result);
            }
            catch (Exception error) { context.Fail("Invalid batch response JSON", error); }
        }
    }
}
