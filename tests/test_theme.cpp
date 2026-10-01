/*
 * SPDX-FileCopyrightText: 2026 ObiWindu <Obi.wandu@proton.me>
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "theme.h"

#include <KConfigGroup>
#include <KSharedConfig>

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QSet>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTest>
#include <QWidget>
#include <QLabel>
#include <QPushButton>
#include <QStyle>

using namespace KateAi;
using namespace Qt::Literals::StringLiterals;

namespace
{

/** Strips comments before inspecting rule bodies. */
QString withoutComments(const QString &css)
{
    static const QRegularExpression comment(u"/\\*.*?\\*/"_s, QRegularExpression::DotMatchesEverythingOption);
    QString stripped = css;
    stripped.remove(comment);
    return stripped;
}

/** Asserts that every token reference has been substituted. */
void assertNoUnresolvedTokens(const QString &css, const char *what)
{
    const QRegularExpression re(u"@([A-Za-z_][A-Za-z0-9_]*)"_s);
    QStringList leftovers;
    auto it = re.globalMatch(withoutComments(css));
    while (it.hasNext()) {
        leftovers << it.next().captured(1);
    }
    leftovers.removeDuplicates();
    QVERIFY2(leftovers.isEmpty(),
             qPrintable(u"%1 still contains unresolved tokens: %2"_s
                           .arg(QString::fromLatin1(what), leftovers.join(u", "_s))));
}

} // namespace

class TestTheme : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void defaultSkinResolvesEverything()
    {
        Theme *theme = Theme::instance();
        QVERIFY(theme->load(u"default"_s));
        QCOMPARE(theme->name(), u"default"_s);

        const QString widgets = theme->widgetsCss();
        const QString documents = theme->documentCss();

        QVERIFY(!widgets.isEmpty());
        QVERIFY(!documents.isEmpty());
        assertNoUnresolvedTokens(widgets, "default.qss");
        assertNoUnresolvedTokens(documents, "default.html.css");

        // Spot-check that the core semantic tokens came through as colours.
        const QStringList required = {
            u"bg_base"_s, u"bg_surface"_s, u"bg_elevated"_s, u"bg_raised"_s,
            u"text_normal"_s, u"text_faint"_s, u"border"_s, u"accent"_s,
            u"success"_s, u"danger"_s, u"thinking_fg"_s, u"scroll_handle"_s,
        };
        for (const QString &key : required) {
            QVERIFY2(theme->color(key).isValid(), qPrintable(u"invalid token: %1"_s.arg(key)));
        }

        QVERIFY(!theme->color(u"does_not_exist"_s).isValid());
    }

    void everyTokenResolvesToAColour()
    {
        // Each documented token must resolve to a colour.
        Theme *theme = Theme::instance();
        QVERIFY(theme->load(u"default"_s));

        const QString widgets = theme->widgetsCss();
        const QString documents = theme->documentCss();
        const QStringList names = theme->tokenNames();
        // A skin may document tokens the layout does not consume yet.
        QVERIFY(names.size() > 40);

        for (const QString &key : names) {
            const QColor color = theme->color(key);
            QVERIFY2(color.isValid(), qPrintable(u"token did not resolve: %1"_s.arg(key)));
            Q_UNUSED(widgets);
            Q_UNUSED(documents);
        }
    }

    void everyBuiltInSkinLoadsAndResolves()
    {
        // A shipped skin must load on its own, with or without its own .qss.
        Theme *theme = Theme::instance();
        const QStringList names = Theme::builtInNames();
        QVERIFY(names.contains(u"default"_s));
        QVERIFY(names.size() >= 2);

        for (const QString &name : names) {
            QVERIFY2(theme->load(name), qPrintable(u"could not load built-in skin: %1"_s.arg(name)));
            QCOMPARE(theme->name(), name);
            assertNoUnresolvedTokens(theme->widgetsCss(), qPrintable(name));
            assertNoUnresolvedTokens(theme->documentCss(), qPrintable(name));
            assertNoUnresolvedTokens(theme->diffCss(), qPrintable(name));
            QVERIFY2(!theme->diffCss().isEmpty(), qPrintable(u"%1: empty diff sheet"_s.arg(name)));
            for (const QString &key : theme->tokenNames()) {
                QVERIFY2(theme->color(key).isValid(),
                         qPrintable(u"%1: token did not resolve: %2"_s.arg(name, key)));
            }

            // The sheet must survive Qt's parser, or the panel silently falls
            // back to the native palette.
            QWidget root;
            root.setObjectName(u"ChatWidget"_s);
            QTest::failOnWarning(QRegularExpression(u".*Could not parse stylesheet.*"_s));
            root.setStyleSheet(theme->widgetsCss());
        }

        QVERIFY(theme->load(u"default"_s));
    }

    void ayuMirageUsesTheAyuPalette()
    {
        // Guards against drift from the upstream Ayu palette.
        Theme *theme = Theme::instance();
        QVERIFY(theme->load(u"kate-ayu-mirage"_s));

        QCOMPARE(theme->color(u"bg_base"_s), QColor(u"#242936"_s));       // editor.background
        QCOMPARE(theme->color(u"accent"_s), QColor(u"#ffcc66"_s));        // focusBorder
        QCOMPARE(theme->color(u"text_normal"_s), QColor(u"#cccac2"_s));   // editor.foreground
        QCOMPARE(theme->color(u"text_faint"_s), QColor(u"#707a8c"_s));    // foreground
        QCOMPARE(theme->color(u"accent_line"_s), QColor(u"#5ccfe6"_s));   // ayu cyan
        QCOMPARE(theme->color(u"danger"_s), QColor(u"#f07178"_s));        // ayu red
        QCOMPARE(theme->color(u"success"_s), QColor(u"#87d96c"_s));       // gitDecoration untracked
        // The skin reuses the built-in layout.
        QVERIFY(theme->widgetsCss().contains(u"QPushButton#sendButton"_s));

        QVERIFY(theme->load(u"default"_s));
    }

    void configuredSkinIsRestored()
    {
        // The chosen skin lives in the config; a fresh panel picks it up through
        // Theme::loadConfigured().
        QStandardPaths::setTestModeEnabled(true);
        KConfigGroup group(KSharedConfig::openConfig(), u"KateAI"_s);
        group.writeEntry(u"ThemeName"_s, u"kate-ayu-mirage"_s);
        group.sync();

        Theme *theme = Theme::instance();
        QVERIFY(theme->loadConfigured());
        QCOMPARE(theme->name(), u"kate-ayu-mirage"_s);

        // An unknown configured name must not leave the panel unstyled.
        group.writeEntry(u"ThemeName"_s, u"does-not-exist"_s);
        group.sync();
        QVERIFY(theme->loadConfigured());
        QCOMPARE(theme->name(), u"default"_s);

        group.deleteEntry(u"ThemeName"_s);
        group.sync();
        QStandardPaths::setTestModeEnabled(false);
        QVERIFY(theme->load(u"default"_s));
    }

    void toolCardTokensAreDefinedAndDiffSheetIsUsable()
    {
        // The tool card draws from its own sheet: every token it names must
        // resolve for all built-in skins.
        Theme *theme = Theme::instance();
        const QStringList cardTokens = {
            u"tool_bg_running"_s, u"tool_bg_done_ok"_s, u"tool_bg_done_fail"_s,
            u"tool_code_bg"_s, u"tool_code_border"_s, u"tool_meta_bg"_s,
            u"tool_meta_border"_s, u"tool_risk_write"_s,
            u"diff_add_fg"_s, u"diff_add_bg"_s, u"diff_remove_fg"_s,
            u"diff_remove_bg"_s, u"diff_hunk_fg"_s,
        };
        for (const QString &skin : Theme::builtInNames()) {
            QVERIFY(theme->load(skin));
            for (const QString &key : cardTokens) {
                QVERIFY2(theme->color(key).isValid(),
                         qPrintable(u"%1: card token missing: %2"_s.arg(skin, key)));
            }
            const QString diff = theme->diffCss();
            QVERIFY(diff.contains(theme->token(u"diff_add_fg"_s)));
            QVERIFY(diff.contains(theme->token(u"diff_remove_bg"_s)));
            QVERIFY(diff.contains(theme->token(u"diff_hunk_fg"_s)));
        }
        QVERIFY(theme->load(u"default"_s));
    }

    void skinDirectoryIsAppNameIndependent()
    {
        // Regression guard: AppDataLocation appends the running application
        // name, which put skins in ~/.local/share/kate/themes.  The location must
        // be a generic base plus an explicit "kateai" component.
        QVERIFY(qEnvironmentVariableIsEmpty("KATEAI_THEME_DIR"));

        const QString generic = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation);
        QVERIFY(!generic.isEmpty());
        QCOMPARE(Theme::userThemeDir(), QDir::cleanPath(generic) + u"/kateai/themes"_s);

        // Independent of the host application name.
        const QString original = QCoreApplication::applicationName();
        QCoreApplication::setApplicationName(u"kate"_s);
        QCOMPARE(Theme::userThemeDir(), QDir::cleanPath(generic) + u"/kateai/themes"_s);
        QCoreApplication::setApplicationName(u"some-other-host"_s);
        QCOMPARE(Theme::userThemeDir(), QDir::cleanPath(generic) + u"/kateai/themes"_s);
        QCoreApplication::setApplicationName(original);

        // $KATEAI_THEME_DIR still wins.
        qputenv("KATEAI_THEME_DIR", QByteArray("/tmp/kateai-skins"));
        QCOMPARE(Theme::userThemeDir(), u"/tmp/kateai-skins"_s);
        qunsetenv("KATEAI_THEME_DIR");
    }

    void unknownSkinFallsBackToDefault()
    {
        Theme *theme = Theme::instance();
        QVERIFY(theme->load(u"no-such-skin"_s));
        QCOMPARE(theme->name(), u"default"_s);
        QVERIFY(!theme->widgetsCss().isEmpty());
    }

    void widgetsSkinParsesAsQtStyleSheet()
    {
        // A malformed sheet is silently ignored by Qt (it only logs a warning),
        // which would leave the whole chat panel unstyled.  Applying the sheet
        // to a real widget tree turns that warning into a test failure.
        Theme *theme = Theme::instance();
        QVERIFY(theme->load(u"default"_s));

        QWidget root;
        root.setObjectName(u"ChatWidget"_s);
        QPushButton button(&root);
        button.setObjectName(u"sendButton"_s);
        QLabel label(&root);
        label.setObjectName(u"userMessageHeader"_s);

        QTest::failOnWarning(QRegularExpression(u".*Could not parse stylesheet.*"_s));
        root.setStyleSheet(theme->widgetsCss());
        root.show();
        QVERIFY(QTest::qWaitForWindowExposed(&root));
        button.setProperty("mode", u"stop"_s);
        root.setStyleSheet(theme->widgetsCss());
    }

    void propertySelectorsRepaintOnRepolish()
    {
        // The stateful buttons (thinking, reasoning effort, send) no longer
        // swap style sheets in C++; they flip a dynamic property and repolish.
        // This guards that Qt actually re-applies the rule when they do.
        Theme *theme = Theme::instance();
        QVERIFY(theme->load(u"default"_s));

        QWidget root;
        QPushButton button(&root);
        button.setObjectName(u"thinkingButton"_s);
        button.setProperty("thinking", false);
        root.setStyleSheet(theme->widgetsCss());
        root.show();
        QVERIFY(QTest::qWaitForWindowExposed(&root));

        const QColor idle = button.palette().color(QPalette::Button);
        QCOMPARE(idle, theme->color(u"bg_elevated"_s));

        button.setProperty("thinking", true);
        button.style()->unpolish(&button);
        button.style()->polish(&button);

        QCOMPARE(button.palette().color(QPalette::Button), theme->color(u"success_deep"_s));
    }

    void everyObjectNamedSelectorExists()
    {
        // Guards against the classic refactor slip: renaming a widget in C++
        // and forgetting the matching selector here (or the other way round).
        Theme *theme = Theme::instance();
        QVERIFY(theme->load(u"default"_s));

        const QSet<QString> known = {
            u"transcriptContainer"_s, u"assistantBrowser"_s, u"thinkingBrowser"_s,
            u"copyButton"_s, u"userMessageCard"_s, u"activityPill"_s,
            u"jumpToLatest"_s, u"toolbarIconButton"_s, u"composerStatus"_s,
            u"modelSelector"_s, u"userMessageBody"_s, u"reasoningEffortButton"_s,
            u"thinkingIndicator"_s, u"thinkingToggle"_s, u"thinkingButton"_s,
            u"welcomeSubtitle"_s, u"welcomeChip"_s, u"assistantPulse"_s,
            u"assistantHeader"_s, u"transcriptScroll"_s, u"composerContainer"_s,
            u"modelFilter"_s, u"workingIndicator"_s, u"tokenCount"_s,
            u"userMessageHeader"_s, u"welcomeIcon"_s, u"welcomeTitle"_s,
            u"composerCard"_s, u"infoBar"_s, u"promptEdit"_s, u"sendButton"_s,
            u"planHeader"_s, u"planStep"_s, u"assistantIcon"_s, u"threadTitle"_s,
            u"chatToolbar"_s, u"welcomeWidget"_s, u"indicatorsContainer"_s,
        };

        // Only rule preludes are inspected: substituted colour values also look
        // like "#rrggbb" and must not be mistaken for selector names.
        const QString css = withoutComments(theme->widgetsCss());
        QString preludes;
        const QStringList blocks = css.split(u"{"_s);
        for (int i = 0; i < blocks.size() - 1; ++i) {
            const QString &block = blocks.at(i);
            preludes += block.mid(block.lastIndexOf(u"}"_s) + 1) + u'\n';
        }

        const QRegularExpression selector(u"#([A-Za-z_][A-Za-z0-9_]*)"_s);
        QStringList unknown;
        auto it = selector.globalMatch(preludes);
        while (it.hasNext()) {
            const QString name = it.next().captured(1);
            if (!known.contains(name) && !unknown.contains(name)) {
                unknown << name;
            }
        }
        QVERIFY2(unknown.isEmpty(),
                 qPrintable(u"selectors without a known widget: %1"_s.arg(unknown.join(u", "_s))));
    }

    void diskSkinOverridesTokensAndSurvivesReload()
    {
        // Customisation workflow: a disk .theme with a couple of overrides must
        // reuse the built-in layout and apply the new values.
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString path = dir.filePath(u"probe.theme"_s);
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
        file.write("@bg_base = #123456\n@accent = #654321\n");
        file.close();

        qputenv("KATEAI_THEME_DIR", dir.path().toLocal8Bit());
        Theme *theme = Theme::instance();
        QVERIFY(theme->load(u"probe"_s));
        QCOMPARE(theme->name(), u"probe"_s);
        QCOMPARE(theme->color(u"bg_base"_s), QColor(u"#123456"_s));
        QCOMPARE(theme->color(u"accent"_s), QColor(u"#654321"_s));
        // Values not overridden come from the built-in defaults.
        QCOMPARE(theme->color(u"text_normal"_s), QColor(u"#e4e4e4"_s));
        // The widget layout comes from the build.
        QVERIFY(theme->widgetsCss().contains(u"QPushButton#sendButton"_s));
        QVERIFY(theme->widgetsCss().contains(u"#123456"_s));
        assertNoUnresolvedTokens(theme->widgetsCss(), "probe.qss");

        // A skin that ships its own .qss replaces the built-in layout.
        QFile qss(dir.filePath(u"probe.qss"_s));
        QVERIFY(qss.open(QIODevice::WriteOnly | QIODevice::Truncate));
        qss.write("QPushButton#sendButton { background-color: @accent; }\n");
        qss.close();
        QVERIFY(theme->load(u"probe"_s));
        const QString custom = theme->widgetsCss();
        QVERIFY(custom.contains(u"QPushButton#sendButton { background-color: #654321; }"_s));
        // The built-in layout is gone: its own sendButton rule is not in there.
        QVERIFY(!custom.contains(u"font-size: 13px;\n    border: none;\n    border-radius: 4px;"_s));

        qunsetenv("KATEAI_THEME_DIR");
        QVERIFY(theme->load(u"default"_s));
    }
};

QTEST_MAIN(TestTheme)
#include "test_theme.moc"
