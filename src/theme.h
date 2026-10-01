/*
 * SPDX-FileCopyrightText: 2026 ObiWindu <Obi.wandu@proton.me>
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include <QColor>
#include <QHash>
#include <QObject>
#include <QString>
#include <QStringList>

namespace KateAi
{

/**
 * Resolves a named skin into Qt style sheets.
 *
 * A skin is up to four assets, each optional:
 *   <name>.theme       "@key = value" token definitions
 *   <name>.qss         QWidget style sheet
 *   <name>.html.css    QTextDocument sheet (markdown)
 *   <name>.diff.css    QTextDocument sheet (tool card diff)
 *
 * Assets are read from disk first, so a skin can be edited without rebuilding,
 * and fall back to the resources compiled into the plugin.  "@token"
 * placeholders are substituted with resolved values at load time.
 */
class Theme : public QObject
{
    Q_OBJECT

public:
    /** Skin used when none is configured. */
    static constexpr const char *DefaultName = "default";

    static Theme *instance();

    /** Currently active skin name. */
    QString name() const { return m_name; }

    /**
     * Loads @p name from disk, then resources; falls back to the built-in
     * default when it cannot be resolved.  Emits themeChanged().
     */
    bool load(const QString &name);

    /** Re-reads the current skin from disk. */
    bool reload() { return load(m_name); }

    /** Loads the skin named by the KateAI "ThemeName" entry. */
    bool loadConfigured();

    /** Style sheet for QWidget painting. */
    QString widgetsCss() const { return m_widgetsCss; }

    /** Style sheet for QTextDocument (assistant markdown). */
    QString documentCss() const { return m_documentCss; }

    /** Style sheet for the read-only panes inside tool cards. */
    QString diffCss() const { return m_diffCss; }

    /** Resolved token value; empty when unknown. */
    QString token(const QString &key) const;

    /** Color for @p key; invalid when unknown. */
    QColor color(const QString &key) const;

    /** Names of the tokens defined by the active skin, sorted. */
    QStringList tokenNames() const;

    /** Names of the compiled-in skins, in menu order. */
    static QStringList builtInNames();

    /**
     * Directory skins are read from: $KATEAI_THEME_DIR when set, otherwise
     * <GenericDataLocation>/kateai/themes.  Disk takes precedence over the
     * compiled-in resources.
     */
    static QString userThemeDir();

    /** Selectable skins: compiled-in first, then user themes. */
    static QStringList availableNames();

Q_SIGNALS:
    void themeChanged();

private:
    Theme();
    ~Theme() override;

    struct SkinAssets {
        QString theme;
        QString qss;
        QString htmlCss;
        QString diffCss;
        bool found = false; // true when the skin exists at all
    };

    SkinAssets readSkin(const QString &name) const;
    void buildCss(const QString &themeText, const QString &qssText, const QString &htmlCssText,
                  const QString &diffCssText);
    void applyDefaults();
    void resolveTokens();
    QString expand(const QString &text) const;

    struct Token {
        QString raw;       // as authored, may itself contain @references
        QString resolved;  // fully expanded value
        bool derived = false;
    };

    QString m_name;
    QHash<QString, Token> m_tokens;
    QString m_widgetsCss;
    QString m_documentCss;
    QString m_diffCss;
    QStringList m_warnings;
};

} // namespace KateAi
