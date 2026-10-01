/*
 * SPDX-FileCopyrightText: 2026 ObiWindu <Obi.wandu@proton.me>
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "theme.h"

#include <KConfigGroup>
#include <KSharedConfig>

#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QSet>
#include <QStandardPaths>

#include <algorithm>
#include <functional>

using namespace Qt::Literals::StringLiterals;

namespace KateAi
{
namespace
{

constexpr const char *kResourceRoot = ":/kateai/themes";
constexpr const char *kAssetSuffixTheme = ".theme";
constexpr const char *kAssetSuffixQss = ".qss";
constexpr const char *kAssetSuffixHtmlCss = ".html.css";
constexpr const char *kAssetSuffixDiffCss = ".diff.css";

QString readTextFile(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return QString();
    }
    return QString::fromUtf8(file.readAll());
}

/** Lighten (positive percent) or darken (negative percent) a color. */
QString shade(const QColor &color, int percent)
{
    if (!color.isValid()) {
        return QString();
    }
    return percent >= 0 ? color.lighter(100 + percent).name() : color.darker(100 - percent).name();
}

} // namespace

Theme::Theme() = default;

Theme::~Theme() = default;

Theme *Theme::instance()
{
    static Theme theme;
    return &theme;
}

QString Theme::userThemeDir()
{
    // Packagers can relocate the skin search path, e.g. a system-wide
    // /usr/share/kateai/themes.
    const QByteArray override = qgetenv("KATEAI_THEME_DIR");
    if (!override.isEmpty()) {
        return QString::fromLocal8Bit(override);
    }
    // GenericDataLocation, not AppDataLocation: the latter appends the running
    // application name, which inside Kate yields ~/.local/share/kate/themes.
    // The /kateai component is explicit so the location stays stable.
    const QString base = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation);
    if (base.isEmpty()) {
        return QString();
    }
    return base + u"/kateai/themes"_s;
}

QStringList Theme::availableNames()
{
    // Compiled-in skins first, then user themes.
    QStringList names;
    const QStringList builtIn = builtInNames();
    for (const QString &name : builtIn) {
        names << name;
    }

    const QDir dir(userThemeDir());
    if (dir.exists()) {
        const QStringList entries = dir.entryList({u"*.theme"_s}, QDir::Files);
        for (const QString &entry : entries) {
            const QString stem = QFileInfo(entry).completeBaseName();
            if (!stem.isEmpty() && !names.contains(stem)) {
                names << stem;
            }
        }
    }

    return names;
}

QStringList Theme::builtInNames()
{
    return {
        QString::fromLatin1(DefaultName),
        u"kate-ayu-mirage"_s,
    };
}

Theme::SkinAssets Theme::readSkin(const QString &name) const
{
    SkinAssets assets;
    const QDir diskDir(userThemeDir());

    // Disk wins over resources so a skin can be edited without a rebuild.  Each
    // asset is looked up independently.
    const auto load = [&](const QString &suffix, const QString &resourceName, bool &fromDisk) {
        fromDisk = false;
        if (diskDir.exists()) {
            const QString diskPath = diskDir.filePath(name + suffix);
            if (QFileInfo::exists(diskPath)) {
                const QString text = readTextFile(diskPath);
                if (!text.isEmpty()) {
                    fromDisk = true;
                    return text;
                }
            }
        }
        return readTextFile(QString::fromLatin1(kResourceRoot) + u'/' + resourceName);
    };

    bool themeFromDisk = false;
    bool qssFromDisk = false;
    bool htmlFromDisk = false;
    bool diffFromDisk = false;
    assets.theme = load(QString::fromLatin1(kAssetSuffixTheme), name + QString::fromLatin1(kAssetSuffixTheme), themeFromDisk);
    assets.qss = load(QString::fromLatin1(kAssetSuffixQss), name + QString::fromLatin1(kAssetSuffixQss), qssFromDisk);
    assets.htmlCss = load(QString::fromLatin1(kAssetSuffixHtmlCss), name + QString::fromLatin1(kAssetSuffixHtmlCss), htmlFromDisk);
    assets.diffCss = load(QString::fromLatin1(kAssetSuffixDiffCss), name + QString::fromLatin1(kAssetSuffixDiffCss), diffFromDisk);

    // A skin exists as soon as one of its assets resolved.
    assets.found = !assets.theme.isEmpty() || !assets.qss.isEmpty() || !assets.htmlCss.isEmpty();

    // Assets the skin does not ship fall back to the built-in ones.  The token
    // file is deliberately left empty when absent: applyDefaults() then supplies
    // the semantic defaults, and built-in skins read their tokens from the
    // resource rather than from disk.
    if (!qssFromDisk) {
        assets.qss = readTextFile(QString::fromLatin1(kResourceRoot) + u"/default.qss"_s);
    }
    if (!htmlFromDisk) {
        assets.htmlCss = readTextFile(QString::fromLatin1(kResourceRoot) + u"/default.html.css"_s);
    }
    if (!diffFromDisk) {
        assets.diffCss = readTextFile(QString::fromLatin1(kResourceRoot) + u"/default.diff.css"_s);
    }

    return assets;
}

void Theme::applyDefaults()
{
    // Semantic defaults, mirroring the token documentation in default.theme; a
    // skin only lists what it changes.
    const QHash<QString, QString> defaults = {
        // surfaces
        {u"bg_base"_s, u"#181818"_s},
        {u"bg_surface"_s, u"#1a1a1a"_s},
        {u"bg_elevated"_s, u"#2e2e32"_s},
        {u"bg_raised"_s, u"#232326"_s},
        {u"bg_raised_alt"_s, u"#202024"_s},
        {u"bg_sunken"_s, u"#1f1f22"_s},
        {u"bg_input"_s, u"#1a1a1a"_s},
        {u"bg_code"_s, u"#222225"_s},
        {u"bg_inline_code"_s, u"#28282d"_s},
        {u"bg_menu"_s, u"#252528"_s},
        // text
        {u"text_strong"_s, u"#ffffff"_s},
        {u"text_normal"_s, u"#e4e4e4"_s},
        {u"text_assistant"_s, u"#d4d4d4"_s},
        {u"text_muted"_s, u"#c8c8c8"_s},
        {u"text_dim"_s, u"#cccccc"_s},
        {u"text_soft"_s, u"#aaaaaa"_s},
        {u"text_faint"_s, u"#888888"_s},
        {u"text_subtle"_s, u"#777777"_s},
        {u"text_placeholder"_s, u"#666666"_s},
        {u"text_disabled"_s, u"#555555"_s},
        // borders
        {u"border"_s, u"#3c3c40"_s},
        {u"border_soft"_s, u"#38383e"_s},
        {u"border_card"_s, u"#333338"_s},
        {u"border_strong"_s, u"#4a4a50"_s},
        {u"border_hover"_s, u"#4a4a52"_s},
        // accent
        {u"accent"_s, u"#007acc"_s},
        {u"accent_hover"_s, u"#0099ff"_s},
        {u"accent_alt"_s, u"#2563eb"_s},
        {u"accent_alt_hover"_s, u"#1d4ed8"_s},
        {u"accent_deep"_s, u"#0062a3"_s},
        {u"accent_ring"_s, u"#60a5fa"_s},
        {u"accent_line"_s, u"#3b82f6"_s},
        {u"accent_line_hover"_s, u"#33bbff"_s},
        {u"accent_text"_s, u"#ffffff"_s},
        // semantic states
        {u"success"_s, u"#22c55e"_s},
        {u"success_bg"_s, u"#1a3320"_s},
        {u"success_border"_s, u"#2d9f42"_s},
        {u"success_hover"_s, u"#3ecf52"_s},
        {u"success_deep"_s, u"#1e7e34"_s},
        {u"success_deep_hover"_s, u"#2d9f42"_s},
        {u"danger"_s, u"#e74c3c"_s},
        {u"danger_hover"_s, u"#ff6b5a"_s},
        {u"warn_fg"_s, u"#ff8888"_s},
        {u"warn_info"_s, u"#ffaa00"_s},
        {u"thinking_fg"_s, u"#f59e0b"_s},
        {u"thinking_bg"_s, u"#3d2e0e"_s},
        {u"thinking_line"_s, u"#3b82f6"_s},
        // scrollbars
        {u"scroll_handle"_s, u"#333338"_s},
        {u"scroll_handle_hover"_s, u"#4a4a52"_s},
        // tool call cards
        {u"tool_bg_running"_s, u"#1a1a2e"_s},
        {u"tool_bg_done_ok"_s, u"#1a1f1a"_s},
        {u"tool_bg_done_fail"_s, u"#1f1a1a"_s},
        {u"tool_code_bg"_s, u"#11131a"_s},
        {u"tool_code_border"_s, u"#2a3a22"_s},
        {u"tool_risk_write"_s, u"#eab308"_s},
        {u"tool_meta_bg"_s, u"#1a1a1a"_s},
        {u"tool_meta_border"_s, u"#3c3c3c"_s},
        {u"diff_add_fg"_s, u"#9ed36a"_s},
        {u"diff_add_bg"_s, u"#1a3a1a"_s},
        {u"diff_remove_fg"_s, u"#ef9999"_s},
        {u"diff_remove_bg"_s, u"#3a1a1a"_s},
        {u"diff_hunk_fg"_s, u"#888888"_s},
        // brand
        {u"brand"_s, u"#3b82f6"_s},
        {u"banner_bg"_s, u"#1e3a5f"_s},
        {u"banner_border"_s, u"#3b82f6"_s},
    };

    for (auto it = defaults.constBegin(); it != defaults.constEnd(); ++it) {
        const QString key = it.key();
        if (m_tokens.contains(key) && !m_tokens.value(key).raw.trimmed().isEmpty()) {
            continue;
        }
        Token token;
        token.raw = it.value();
        m_tokens.insert(key, token);
    }

    // Derived hover shades save a skin from defining every interaction state.
    // An explicit value in the .theme file wins.
    const auto derive = [this](const QString &key, const QString &source, int percent) {
        if (m_tokens.contains(key) && !m_tokens.value(key).raw.trimmed().isEmpty()) {
            return;
        }
        const QColor base(m_tokens.value(source).raw.trimmed());
        Token token;
        token.raw = shade(base, percent);
        token.derived = true;
        if (!token.raw.isEmpty()) {
            m_tokens.insert(key, token);
        }
    };

    derive(u"bg_hover"_s, u"bg_elevated"_s, 8);
    derive(u"bg_hover_alt"_s, u"bg_raised"_s, 8);
    derive(u"bg_hover_soft"_s, u"bg_raised_alt"_s, 10);
}

void Theme::resolveTokens()
{
    QSet<QString> visiting;
    QSet<QString> done;

    const QString pattern = u"@([A-Za-z_][A-Za-z0-9_]*)"_s;

    // Recursion guard: a cyclic definition degrades to the literal token name
    // instead of hanging the UI.
    std::function<QString(const QString &, int)> resolveKey = [&](const QString &key, int depth) -> QString {
        if (done.contains(key)) {
            return m_tokens.value(key).resolved;
        }
        if (!m_tokens.contains(key)) {
            return QString();
        }
        if (visiting.contains(key) || depth > 12) {
            m_warnings << u"cyclic token reference: @%1"_s.arg(key);
            return m_tokens.value(key).raw;
        }
        visiting.insert(key);

        Token token = m_tokens.value(key);
        static const QRegularExpression re(pattern);
        QString value = token.raw;
        // Repeat so chained references also resolve.
        for (int pass = 0; pass < 12; ++pass) {
            bool replaced = false;
            QString next;
            int last = 0;
            auto it = re.globalMatch(value);
            while (it.hasNext()) {
                const QRegularExpressionMatch match = it.next();
                const QString ref = match.captured(1);
                if (!m_tokens.contains(ref)) {
                    continue; // leave unknown tokens for Qt to ignore
                }
                next += value.mid(last, match.capturedStart() - last);
                next += resolveKey(ref, depth + 1);
                last = match.capturedEnd();
                replaced = true;
            }
            if (!replaced) {
                break;
            }
            next += value.mid(last);
            value = next;
        }

        visiting.remove(key);
        done.insert(key);
        token.resolved = value;
        m_tokens.insert(key, token);
        return value;
    };

    const QStringList keys = m_tokens.keys();
    for (const QString &key : keys) {
        resolveKey(key, 0);
    }
}

QString Theme::expand(const QString &text) const
{
    static const QRegularExpression re(u"@([A-Za-z_][A-Za-z0-9_]*)"_s);
    QString out;
    out.reserve(text.size());
    int last = 0;
    auto it = re.globalMatch(text);
    while (it.hasNext()) {
        const QRegularExpressionMatch match = it.next();
        const QString ref = match.captured(1);
        const auto tokenIt = m_tokens.constFind(ref);
        if (tokenIt == m_tokens.constEnd() || tokenIt->resolved.isEmpty()) {
            continue;
        }
        out += text.mid(last, match.capturedStart() - last);
        out += tokenIt->resolved;
        last = match.capturedEnd();
    }
    out += text.mid(last);
    return out;
}

void Theme::buildCss(const QString &themeText, const QString &qssText, const QString &htmlCssText,
                     const QString &diffCssText)
{
    m_tokens.clear();
    m_warnings.clear();

    // The skin text is parsed directly; default.theme is only consulted through
    // the semantic defaults below.
    for (const QString &line : themeText.split(u'\n')) {
        const QString trimmed = line.trimmed();
        if (trimmed.isEmpty() || trimmed.startsWith(u'#')) {
            continue;
        }
        if (!trimmed.startsWith(u'@')) {
            continue;
        }
        const int eq = trimmed.indexOf(u'=');
        if (eq <= 1) {
            continue;
        }
        const QString key = trimmed.mid(1, eq - 1).trimmed();
        QString value = trimmed.mid(eq + 1).trimmed();
        // A leading '#' starts a colour; a later '#' is an inline comment.
        // Treating them alike silently drops colour-valued overrides.
        const int comment = value.indexOf(u'#', 1);
        if (comment > 0) {
            value = value.left(comment).trimmed();
        }
        if (key.isEmpty() || value.isEmpty()) {
            continue;
        }
        Token token;
        token.raw = value;
        m_tokens.insert(key, token);
    }

    applyDefaults();
    resolveTokens();

    m_widgetsCss = expand(qssText);
    m_documentCss = expand(htmlCssText);
    m_diffCss = expand(diffCssText);
}

bool Theme::load(const QString &requested)
{
    const bool first = m_name.isEmpty();
    QString wanted = requested.trimmed();
    if (wanted.isEmpty()) {
        wanted = QString::fromLatin1(DefaultName);
    }

    SkinAssets assets = readSkin(wanted);
    if (!assets.found) {
        // Unknown skin: announce it and fall back to the compiled default.
        if (wanted != QString::fromLatin1(DefaultName)) {
            qWarning() << "KateAI: unknown theme" << wanted << "- falling back to"
                       << QString::fromLatin1(DefaultName);
            wanted = QString::fromLatin1(DefaultName);
            assets = readSkin(wanted);
        }
    }

    buildCss(assets.theme, assets.qss, assets.htmlCss, assets.diffCss);

    const QString previous = m_name;
    m_name = wanted;
    for (const QString &warning : std::as_const(m_warnings)) {
        qWarning() << "KateAI theme:" << warning;
    }

    if (!first) {
        Q_EMIT themeChanged();
    }
    return !m_widgetsCss.isEmpty();
}

bool Theme::loadConfigured()
{
    // Read from the config directly, before Settings has been propagated.
    const KConfigGroup group(KSharedConfig::openConfig(), u"KateAI"_s);
    const QString configured = group.readEntry(u"ThemeName"_s, QString::fromLatin1(DefaultName));
    return load(configured);
}

QString Theme::token(const QString &key) const
{
    const auto it = m_tokens.constFind(key);
    return it == m_tokens.constEnd() ? QString() : it->resolved;
}

QColor Theme::color(const QString &key) const
{
    return QColor(token(key));
}

QStringList Theme::tokenNames() const
{
    QStringList names = m_tokens.keys();
    names.sort(Qt::CaseInsensitive);
    return names;
}

} // namespace KateAi
