#include "ConfigManager.h"
#include <QUrl>

namespace
{
    QString normalizeBaseUrl(const QString &url)
    {
        QString normalized = url.trimmed();
        while (normalized.endsWith('/'))
        {
            normalized.chop(1);
        }
        return normalized;
    }

    QString encodeBaseUrlKey(const QString &url)
    {
        return QString::fromUtf8(QUrl::toPercentEncoding(url));
    }
}

QString ConfigManager::loadApiKeyForBaseUrl(const QString &baseUrl, const QString &filename)
{
    const QString normalized = normalizeBaseUrl(baseUrl);
    if (normalized.isEmpty())
    {
        return QString();
    }

    QSettings settings(filename, QSettings::IniFormat);
    settings.beginGroup("ApiKeysByBaseUrl");
    const QString apiKey = settings.value(encodeBaseUrlKey(normalized)).toString();
    settings.endGroup();
    return apiKey;
}

QString ConfigManager::loadModelForBaseUrl(const QString &baseUrl, const QString &filename)
{
    const QString normalized = normalizeBaseUrl(baseUrl);
    if (normalized.isEmpty())
    {
        return QString();
    }

    QSettings settings(filename, QSettings::IniFormat);
    settings.beginGroup("ModelByBaseUrl");
    const QString modelName = settings.value(encodeBaseUrlKey(normalized)).toString();
    settings.endGroup();
    return modelName;
}

void ConfigManager::saveApiKeyForBaseUrl(const QString &baseUrl, const QString &apiKey, const QString &filename)
{
    const QString normalized = normalizeBaseUrl(baseUrl);
    if (normalized.isEmpty())
    {
        return;
    }

    QSettings settings(filename, QSettings::IniFormat);
    settings.beginGroup("ApiKeysByBaseUrl");
    settings.setValue(encodeBaseUrlKey(normalized), apiKey);
    settings.endGroup();
    settings.sync();
}

void ConfigManager::removeApiKeyForBaseUrl(const QString &baseUrl, const QString &filename)
{
    const QString normalized = normalizeBaseUrl(baseUrl);
    if (normalized.isEmpty())
    {
        return;
    }

    QSettings settings(filename, QSettings::IniFormat);
    settings.beginGroup("ApiKeysByBaseUrl");
    settings.remove(encodeBaseUrlKey(normalized));
    settings.endGroup();
    settings.sync();
}

void ConfigManager::saveModelForBaseUrl(const QString &baseUrl, const QString &modelName, const QString &filename)
{
    const QString normalized = normalizeBaseUrl(baseUrl);
    if (normalized.isEmpty())
    {
        return;
    }

    QSettings settings(filename, QSettings::IniFormat);
    settings.beginGroup("ModelByBaseUrl");
    settings.setValue(encodeBaseUrlKey(normalized), modelName);
    settings.endGroup();
    settings.sync();
}

void ConfigManager::removeModelForBaseUrl(const QString &baseUrl, const QString &filename)
{
    const QString normalized = normalizeBaseUrl(baseUrl);
    if (normalized.isEmpty())
    {
        return;
    }

    QSettings settings(filename, QSettings::IniFormat);
    settings.beginGroup("ModelByBaseUrl");
    settings.remove(encodeBaseUrlKey(normalized));
    settings.endGroup();
    settings.sync();
}

QString ConfigManager::loadPresetNameForBaseUrl(const QString &baseUrl, const QString &filename)
{
    const QString normalized = normalizeBaseUrl(baseUrl);
    if (normalized.isEmpty())
    {
        return QString();
    }

    QSettings settings(filename, QSettings::IniFormat);
    settings.beginGroup("PresetNameByBaseUrl");
    const QString presetName = settings.value(encodeBaseUrlKey(normalized)).toString();
    settings.endGroup();
    return presetName;
}

void ConfigManager::savePresetNameForBaseUrl(const QString &baseUrl, const QString &presetName, const QString &filename)
{
    const QString normalized = normalizeBaseUrl(baseUrl);
    if (normalized.isEmpty())
    {
        return;
    }

    QSettings settings(filename, QSettings::IniFormat);
    settings.beginGroup("PresetNameByBaseUrl");
    settings.setValue(encodeBaseUrlKey(normalized), presetName);
    settings.endGroup();
    settings.sync();
}

void ConfigManager::removePresetNameForBaseUrl(const QString &baseUrl, const QString &filename)
{
    const QString normalized = normalizeBaseUrl(baseUrl);
    if (normalized.isEmpty())
    {
        return;
    }

    QSettings settings(filename, QSettings::IniFormat);
    settings.beginGroup("PresetNameByBaseUrl");
    settings.remove(encodeBaseUrlKey(normalized));
    settings.endGroup();
    settings.sync();
}

// 实现加载配置的函数
AppConfig ConfigManager::loadConfig(const QString &filename)
{
    QSettings settings(filename, QSettings::IniFormat);
    AppConfig config;

    config.api_address = settings.value("Settings/api_address", config.api_address).toString();
    config.api_key = settings.value("Settings/api_key", config.api_key).toString();

    // 若存在按 base_url 记忆的 key，则优先使用映射中的值
    const QString mappedApiKey = loadApiKeyForBaseUrl(config.api_address, filename);
    if (!mappedApiKey.isEmpty())
    {
        config.api_key = mappedApiKey;
    }

    config.model_name = settings.value("Settings/model_name", config.model_name).toString();

    const QString mappedModelName = loadModelForBaseUrl(config.api_address, filename);
    if (!mappedModelName.isEmpty())
    {
        config.model_name = mappedModelName;
    }
    config.port = settings.value("Settings/port", config.port).toInt();
    config.system_prompt = settings.value("Settings/system_prompt", config.system_prompt).toString();
    config.pre_prompt = settings.value("Settings/pre_prompt", config.pre_prompt).toString();
    config.context_num = settings.value("Settings/context_num", config.context_num).toInt();
    config.temperature = settings.value("Settings/temperature", config.temperature).toDouble();
    config.max_threads = settings.value("Settings/max_threads", config.max_threads).toInt();
    config.language = settings.value("Settings/language", config.language).toInt();

    config.custom_api_urls = settings.value("Settings/custom_api_urls").toStringList();

    // --- 术语表相关设置 ---
    config.enable_glossary = settings.value("Settings/enable_glossary", config.enable_glossary).toBool();

    config.glossary_seed = settings.value("Settings/glossary_seed", config.glossary_seed).toString();
    config.glossary_prompt_seed = settings.value("Settings/glossary_prompt_seed", config.glossary_prompt_seed).toString();
    config.font_name = settings.value("Settings/font_name", config.font_name).toString();
    config.max_concurrency = settings.value("Settings/max_concurrency", config.max_concurrency).toInt();
    config.max_batch_lines = settings.value("Settings/max_batch_lines", config.max_batch_lines).toInt();
    config.translation_enabled = settings.value("Settings/translation_enabled", true).toBool();
    config.agent_api_address = settings.value("Agent/api_address", config.agent_api_address).toString();
    config.agent_api_key = settings.value("Agent/api_key", config.agent_api_key).toString();
    config.agent_model = settings.value("Agent/model", config.agent_model).toString();
    config.glossary_path = settings.value("Settings/glossary_path", config.glossary_path).toString();
    config.glossary_history = settings.value("Settings/glossary_history").toStringList();

    config.lock_system_prompt = settings.value("Settings/lock_system_prompt", false).toBool();
    config.self_evo_enabled = settings.value("Settings/self_evo_enabled", false).toBool();
    config.self_evo_endpoint = settings.value("Settings/self_evo_endpoint").toString();
    config.self_evo_model = settings.value("Settings/self_evo_model").toString();
    config.self_evo_key = settings.value("Settings/self_evo_key").toString();
    config.self_evo_temperature = settings.value("Settings/self_evo_temperature", 0.3).toDouble();
    config.lock_glossary = settings.value("Settings/lock_glossary", false).toBool();

    // --- 🔥 新增：读取 UI 设置 ---
    // 如果配置文件里没有这一项，默认返回 0 (Classic) 和 true (Dark)
    config.is_dark = settings.value("UI/is_dark", true).toBool();


    config.is_dark = settings.value("UI/is_dark", true).toBool();


    config.enable_debug_mode = settings.value("Settings/enable_debug_mode", false).toBool();
    config.enable_batch = settings.value("Settings/enable_batch", false).toBool();         // 默认关闭
    config.handle_rich_text = settings.value("Settings/handle_rich_text", false).toBool(); // 默认关闭
    config.extract_newline = settings.value("Settings/extract_newline", false).toBool();   // 默认开启

    config.max_retries = settings.value("Settings/max_retries", config.max_retries).toInt();
    config.timeout_ms = settings.value("Settings/timeout_ms", config.timeout_ms).toInt();

    config.hijack_from_lang = settings.value("Hijack/from_lang", config.hijack_from_lang).toString();
    config.hijack_to_lang = settings.value("Hijack/to_lang", config.hijack_to_lang).toString();
    config.hijack_text_getter = settings.value("Hijack/text_getter", config.hijack_text_getter).toBool();

    config.hijack_enable_imgui = settings.value("Hijack/enable_imgui", config.hijack_enable_imgui).toBool();
    config.hijack_enable_ugui = settings.value("Hijack/enable_ugui", config.hijack_enable_ugui).toBool();
    config.hijack_enable_ui_elements = settings.value("Hijack/enable_ui_elements", config.hijack_enable_ui_elements).toBool();
    config.hijack_enable_ngui = settings.value("Hijack/enable_ngui", config.hijack_enable_ngui).toBool();
    config.hijack_enable_text_mesh_pro = settings.value("Hijack/enable_text_mesh_pro", config.hijack_enable_text_mesh_pro).toBool();
    config.hijack_enable_text_mesh = settings.value("Hijack/enable_text_mesh", config.hijack_enable_text_mesh).toBool();
    config.hijack_enable_fairy_gui = settings.value("Hijack/enable_fairy_gui", config.hijack_enable_fairy_gui).toBool();
    return config;
}

// 实现保存配置的函数
void ConfigManager::saveConfig(const AppConfig &config, const QString &filename)
{
    QSettings settings(filename, QSettings::IniFormat);

    settings.setValue("Settings/api_address", config.api_address);
    settings.setValue("Settings/api_key", config.api_key);
    saveApiKeyForBaseUrl(config.api_address, config.api_key, filename);
    settings.setValue("Settings/model_name", config.model_name);
    saveModelForBaseUrl(config.api_address, config.model_name, filename);
    settings.setValue("Settings/port", config.port);
    settings.setValue("Settings/system_prompt", config.system_prompt);
    settings.setValue("Settings/pre_prompt", config.pre_prompt);
    settings.setValue("Settings/context_num", config.context_num);
    settings.setValue("Settings/temperature", config.temperature);
    settings.setValue("Settings/max_threads", config.max_threads);
    settings.setValue("Settings/language", config.language);

    settings.setValue("Settings/custom_api_urls", config.custom_api_urls);

    // --- 术语表相关设置 ---
    settings.setValue("Settings/enable_glossary", config.enable_glossary);
    settings.setValue("Settings/glossary_seed", config.glossary_seed);
    settings.setValue("Settings/glossary_prompt_seed", config.glossary_prompt_seed);
    settings.setValue("Settings/font_name", config.font_name);
    settings.setValue("Settings/max_concurrency", config.max_concurrency);
    settings.setValue("Settings/max_batch_lines", config.max_batch_lines);
    settings.setValue("Settings/translation_enabled", config.translation_enabled);
    settings.setValue("Agent/api_address", config.agent_api_address);
    settings.setValue("Agent/api_key", config.agent_api_key);
    settings.setValue("Agent/model", config.agent_model);
    settings.setValue("Settings/glossary_path", config.glossary_path);
    settings.setValue("Settings/glossary_history", config.glossary_history);

    // --- 保存锁定状态 ---
    settings.setValue("Settings/lock_system_prompt", config.lock_system_prompt);
    settings.setValue("Settings/self_evo_enabled", config.self_evo_enabled);
    settings.setValue("Settings/self_evo_endpoint", config.self_evo_endpoint);
    settings.setValue("Settings/self_evo_model", config.self_evo_model);
    settings.setValue("Settings/self_evo_key", config.self_evo_key);
    settings.setValue("Settings/self_evo_temperature", config.self_evo_temperature);
    settings.setValue("Settings/lock_glossary", config.lock_glossary);

    // ==========================================
    // 🔥 CAN: 修复核心区域开始
    // ==========================================

    // 💡 修复点：is_dark 是双界面共用的属性，必须放在防御屏障之外！
    settings.setValue("UI/is_dark", config.is_dark);
    // 🌟 全局透明度：各自独立，互不污染，双双持久化！

    // 只有当配置是流光模式发出时，才允许覆写【流光专属】UI 参数！
    // 这样经典模式保存时，硬盘里的圆角等数据就能作为“中间值”被完美保护！
    {
    }

    // ==========================================
    // 🔥 CAN: 修复核心区域结束
    // ==========================================

    settings.setValue("Settings/enable_debug_mode", config.enable_debug_mode);
    settings.setValue("Settings/enable_batch", config.enable_batch);
    settings.setValue("Settings/handle_rich_text", config.handle_rich_text);
    settings.setValue("Settings/extract_newline", config.extract_newline);

    settings.setValue("Settings/max_retries", config.max_retries);
    settings.setValue("Settings/timeout_ms", config.timeout_ms);

    settings.setValue("Hijack/from_lang", config.hijack_from_lang);
    settings.setValue("Hijack/to_lang", config.hijack_to_lang);

    settings.setValue("Hijack/text_getter", config.hijack_text_getter);

    settings.setValue("Hijack/enable_imgui", config.hijack_enable_imgui);
    settings.setValue("Hijack/enable_ugui", config.hijack_enable_ugui);
    settings.setValue("Hijack/enable_ui_elements", config.hijack_enable_ui_elements);
    settings.setValue("Hijack/enable_ngui", config.hijack_enable_ngui);
    settings.setValue("Hijack/enable_text_mesh_pro", config.hijack_enable_text_mesh_pro);
    settings.setValue("Hijack/enable_text_mesh", config.hijack_enable_text_mesh);
    settings.setValue("Hijack/enable_fairy_gui", config.hijack_enable_fairy_gui);

    settings.sync();
}