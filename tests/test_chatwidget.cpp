/*
 * SPDX-FileCopyrightText: 2026 ObiWindu <Obi.wandu@proton.me>
 * SPDX-License-Identifier: LGPL-2.1-or-later
 *
 * Drives a real ChatWidget through skin switches and conversation restore.
 * themecheck covers the tokens; this covers the widget, where Qt style sheet and
 * QTextDocument/palette state interact, so a live switch can differ from a
 * fresh start.
 */

#include "chatwidget.h"
#include "promptedit.h"
#include "sessionstore.h"
#include "toolcallwidget.h"
#include "theme.h"

#include <QImage>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QLabel>
#include <QStandardPaths>
#include <QStyle>
#include <QTest>
#include <QTextBrowser>

using namespace KateAi;
using namespace Qt::Literals::StringLiterals;

namespace
{

using KateAi::ToolCallWidget;

/** Applies the active skin as ChatWidget::applySkin does. */
void reapplySkin(QWidget *widget)
{
    widget->setStyleSheet(Theme::instance()->widgetsCss());
    const QList<QWidget *> children = widget->findChildren<QWidget *>();
    for (QWidget *child : children) {
        child->style()->unpolish(child);
        child->style()->polish(child);
        child->update();
    }
    widget->style()->unpolish(widget);
    widget->style()->polish(widget);
    widget->update();
}

} // namespace

class TestChatWidgetSkin : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void initTestCase()
    {
        QStandardPaths::setTestModeEnabled(true);
        QVERIFY(Theme::instance()->load(u"default"_s));
    }

    void cleanupTestCase()
    {
        QStandardPaths::setTestModeEnabled(false);
        QVERIFY(Theme::instance()->load(u"default"_s));
    }

    /** A reasoning block keeps its colour across a skin switch. */
    void reasoningBlockKeepsItsColourAcrossSkinSwitch()
    {
        ChatWidget chat;
        chat.resize(600, 700);
        chat.show();
        QVERIFY(QTest::qWaitForWindowExposed(&chat));

        // Drive a reasoning turn through the agent's public signal, the real
        // path that creates the thinking block.
        QVERIFY(QMetaObject::invokeMethod(chat.agent(), "thinkingFinished",
                                          Q_ARG(QString, u"Checking the thinking block colour across a skin switch."_s)));
        QTest::qWait(20);
        QTextBrowser *thinking = chat.findChild<QTextBrowser *>(u"thinkingBrowser"_s);
        QVERIFY2(thinking, "no thinking browser in the chat widget");

        // Live reasoning, then a skin switch while it is on screen.
        reapplySkin(&chat);
        const QColor before = thinking->palette().color(QPalette::Text);
        QVERIFY(before.isValid());

        QVERIFY(Theme::instance()->load(u"kate-ayu-mirage"_s));
        QTest::qWait(50);
        reapplySkin(&chat);

        const QColor after = thinking->palette().color(QPalette::Text);
        const QColor expected = Theme::instance()->color(u"text_muted"_s);
        QVERIFY2(after == expected,
                 qPrintable(u"thinking text colour changed to %1, expected %2"_s
                                .arg(after.name(), expected.name())));

        // And back again, which is where the report says it turns white.
        QVERIFY(Theme::instance()->load(u"default"_s));
        QTest::qWait(50);
        reapplySkin(&chat);
        QCOMPARE(thinking->palette().color(QPalette::Text), Theme::instance()->color(u"text_muted"_s));
    }

    /** A skin switch during a live turn must not blank the transcript. */
    void midTurnSkinSwitchKeepsReasoningTextVisible()
    {
        ChatWidget chat;
        chat.resize(600, 700);
        chat.show();
        QVERIFY(QTest::qWaitForWindowExposed(&chat));
        reapplySkin(&chat);

        // Live turn: reasoning arrives, then the visible answer starts streaming
        // while the working indicator and the pacer are running.
        QVERIFY(QMetaObject::invokeMethod(chat.agent(), "thinkingDelta",
                                          Q_ARG(QString, u"The reasoning streams while the skin is switched."_s)));
        QVERIFY(QMetaObject::invokeMethod(chat.agent(), "assistantDelta",
                                          Q_ARG(QString, u"Partial answer"_s)));
        QTest::qWait(30);

        QTextBrowser *thinking = chat.findChild<QTextBrowser *>(u"thinkingBrowser"_s);
        QVERIFY(thinking);
        thinking->setFixedHeight(80);
        reapplySkin(&chat);
        QTest::qWait(20);

        // Switch skins mid-stream and let every deferred relayout run.
        QVERIFY(Theme::instance()->load(u"kate-ayu-mirage"_s));
        QTest::qWait(80);
        QVERIFY(QMetaObject::invokeMethod(chat.agent(), "assistantDelta",
                                          Q_ARG(QString, u" and the rest of the answer."_s)));
        QTest::qWait(80);

        QTextBrowser *stillThere = chat.findChild<QTextBrowser *>(u"thinkingBrowser"_s);
        QVERIFY2(stillThere, "the reasoning block disappeared across the skin switch");
        const QColor expected = Theme::instance()->color(u"text_muted"_s);
        QCOMPARE(stillThere->palette().color(QPalette::Text), expected);

        // Render the whole panel so the result can be inspected as an image.
        QTest::qWait(120);
        const QImage whole = chat.grab().toImage();
        QVERIFY(!whole.isNull());
        QVERIFY(whole.save(QStringLiteral("/home/abola/Documents/KateAI/build-abola/chat-live-switch.png")));
        qWarning() << "RENDER saved, theme" << Theme::instance()->name()
                   << "thinking palette" << stillThere->palette().color(QPalette::Text).name();
    }

    /** A reasoning block created around a switch must still be styled. */
    void reasoningBlockCreatedDuringSwitchKeepsItsColour()
    {
        ChatWidget chat;
        chat.resize(600, 700);
        chat.show();
        QVERIFY(QTest::qWaitForWindowExposed(&chat));
        reapplySkin(&chat);

        // Reasoning is in flight (indicator showing, nothing rendered yet) when
        // the user hits Apply in the config dialog.
        QVERIFY(QMetaObject::invokeMethod(chat.agent(), "thinkingDelta",
                                          Q_ARG(QString, u"streaming"_s)));
        QTest::qWait(20);
        QVERIFY(Theme::instance()->load(u"kate-ayu-mirage"_s));
        QTest::qWait(20);

        // The block materialises after the switch.
        QVERIFY(QMetaObject::invokeMethod(chat.agent(), "thinkingFinished",
                                          Q_ARG(QString, u"Reasoning that finishes after the skin switch."_s)));
        QTest::qWait(40);

        QTextBrowser *thinking = chat.findChild<QTextBrowser *>(u"thinkingBrowser"_s);
        QVERIFY(thinking);
        QCOMPARE(thinking->palette().color(QPalette::Text), Theme::instance()->color(u"text_muted"_s));
        QVERIFY(thinking->document()->defaultStyleSheet().contains(Theme::instance()->token(u"text_muted"_s)));

        // Visible answer after the switch, then another switch under it.
        QVERIFY(QMetaObject::invokeMethod(chat.agent(), "assistantDelta",
                                          Q_ARG(QString, u"Answer after the switch."_s)));
        QTest::qWait(30);
        QVERIFY(Theme::instance()->load(u"default"_s));
        QTest::qWait(60);
        QCOMPARE(thinking->palette().color(QPalette::Text), Theme::instance()->color(u"text_muted"_s));
    }

    /** Widgets built after a switch pick the active skin up immediately. */
    void widgetsCreatedAfterASwitchAreSkinned()
    {
        QVERIFY(Theme::instance()->load(u"kate-ayu-mirage"_s));

        ChatWidget chat;
        chat.resize(600, 700);
        chat.show();
        QVERIFY(QTest::qWaitForWindowExposed(&chat));

        // A user message card is built on demand; it must already be dark.
        QVERIFY(QMetaObject::invokeMethod(chat.agent(), "userMessage",
                                          Q_ARG(QString, u"Built after the switch."_s)));
        QTest::qWait(30);

        QWidget *card = chat.findChild<QWidget *>(u"userMessageCard"_s);
        QVERIFY(card);
        QCOMPARE(card->palette().color(QPalette::Window), Theme::instance()->color(u"bg_raised"_s));
        QCOMPARE(card->palette().color(QPalette::Base), Theme::instance()->color(u"bg_raised"_s));

        QVERIFY(Theme::instance()->load(u"default"_s));
    }

    /**
     * A tool card sets its own style sheet, so a skin switch must be propagated
     * to it; otherwise it falls back to the desktop palette.
     */
    void toolCardKeepsReadableTextAcrossSkinSwitch()
    {
        ChatWidget chat;
        chat.resize(600, 700);
        chat.show();
        QVERIFY(QTest::qWaitForWindowExposed(&chat));

        // A bash tool card: command line plus output.
        PermissionRequest request;
        request.toolCallId = u"call-1"_s;
        request.toolName = u"bash"_s;
        request.summary = u"ls -la /tmp"_s;
        request.risk = ToolRisk::Execute;
        QVERIFY(QMetaObject::invokeMethod(chat.agent(), "toolStarted",
                                          Q_ARG(KateAi::PermissionRequest, request)));
        QTest::qWait(30);

        ToolCallWidget *card = chat.findChild<ToolCallWidget *>();
        QVERIFY2(card, "no tool call card in the widget");

        // The command line is a QLabel painted from the card's own sheet, so it
        // must carry the skin text colour rather than the desktop default.
        const auto checkTextColour = [&card](const QString &themeName) {
            const QColor expected = Theme::instance()->color(u"text_dim"_s);
            QLabel *title = card->findChild<QLabel *>(u"toolCardTitle"_s);
            if (!title) {
                return u"%1: tool card has no titled label"_s.arg(themeName);
            }
            const QColor text = title->palette().color(QPalette::WindowText);
            if (text != expected) {
                return u"%1: tool card text is %2, expected %3"_s.arg(themeName, text.name(), expected.name());
            }
            // A near-white label would be the regression.
            const QList<QLabel *> labels = card->findChildren<QLabel *>();
            for (QLabel *label : labels) {
                const QColor colour = label->palette().color(QPalette::WindowText);
                if (colour.lightness() > 0xD0 && expected.lightness() < 0xA0) {
                    return u"%1: label %2 still paints near-white (%3)"_s
                        .arg(themeName, label->objectName(), colour.name());
                }
            }
            return QString();
        };

        reapplySkin(&chat);
        QVERIFY2(checkTextColour(u"default"_s).isEmpty(), qPrintable(checkTextColour(u"default"_s)));

        QVERIFY(Theme::instance()->load(u"kate-ayu-mirage"_s));
        QTest::qWait(50);
        reapplySkin(&chat);
        QVERIFY2(checkTextColour(u"kate-ayu-mirage"_s).isEmpty(), qPrintable(checkTextColour(u"kate-ayu-mirage"_s)));

        // The card's own colours must still be painted.
        QVERIFY(!card->styleSheet().isEmpty());

        // A card created after the switch starts with the right colours.
        PermissionRequest later;
        later.toolCallId = u"call-2"_s;
        later.toolName = u"bash"_s;
        later.summary = u"echo hello"_s;
        later.risk = ToolRisk::Execute;
        QVERIFY(QMetaObject::invokeMethod(chat.agent(), "toolStarted",
                                          Q_ARG(KateAi::PermissionRequest, later)));
        QTest::qWait(30);
        const QList<ToolCallWidget *> cards = chat.findChildren<ToolCallWidget *>();
        QCOMPARE(cards.size(), 2);
        ToolCallWidget *fresh = cards.at(1);
        QLabel *freshTitle = fresh->findChild<QLabel *>(u"toolCardTitle"_s);
        QVERIFY(freshTitle);
        QCOMPARE(freshTitle->palette().color(QPalette::WindowText), Theme::instance()->color(u"text_dim"_s));

        QVERIFY(Theme::instance()->load(u"default"_s));
        QTest::qWait(50);
        reapplySkin(&chat);
        QVERIFY2(checkTextColour(u"default"_s).isEmpty(), qPrintable(checkTextColour(u"default"_s)));
    }





    /** Every text view must keep a palette that matches the active skin. */
    void allTextBrowsersFollowTheSkin()
    {
        ChatWidget chat;
        chat.resize(600, 700);
        chat.show();
        QVERIFY(QTest::qWaitForWindowExposed(&chat));

        // Both QTextBrowser kinds must exist: reasoning (thinkingFinished) and
        // answer (assistantDelta).
        QVERIFY(QMetaObject::invokeMethod(chat.agent(), "thinkingFinished",
                                          Q_ARG(QString, u"Reasoning text."_s)));
        QVERIFY(QMetaObject::invokeMethod(chat.agent(), "assistantDelta",
                                          Q_ARG(QString, u"Visible answer with `inline code`."_s)));
        QTest::qWait(20);
        QVERIFY(!chat.findChildren<QTextBrowser *>().isEmpty());

        reapplySkin(&chat);

        const auto check = [&chat](const char *themeName) {
            const Theme *theme = Theme::instance();
            const QList<QTextBrowser *> browsers = chat.findChildren<QTextBrowser *>();
            if (browsers.isEmpty()) {
                return u"%1: no text browsers in the widget"_s.arg(QString::fromLatin1(themeName));
            }
            for (QTextBrowser *browser : browsers) {
                const QString name = browser->objectName();
                // Every rich-text view paints from the skin's document sheet.
                const QString sheet = browser->document()->defaultStyleSheet();
                const QString expectedDocument = theme->token(u"text_assistant"_s);
                if (!sheet.contains(expectedDocument)) {
                    return u"%1: %2 document sheet lost the skin colour %3"_s
                        .arg(QString::fromLatin1(themeName), name, expectedDocument);
                }
                // The reasoning view also drives its colour through the palette.
                if (name == u"thinkingBrowser"_s) {
                    const QColor expected = theme->color(u"text_muted"_s);
                    const QColor text = browser->palette().color(QPalette::Text);
                    if (text != expected) {
                        return u"%1: thinking palette is %2, expected %3"_s
                            .arg(QString::fromLatin1(themeName), text.name(), expected.name());
                    }
                }
            }
            return QString();
        };

        QVERIFY(Theme::instance()->load(u"kate-ayu-mirage"_s));
        QTest::qWait(50);
        reapplySkin(&chat);
        QVERIFY2(check("kate-ayu-mirage").isEmpty(), qPrintable(check("kate-ayu-mirage")));

        QVERIFY(Theme::instance()->load(u"default"_s));
        QTest::qWait(50);
        reapplySkin(&chat);
        QVERIFY2(check("default").isEmpty(), qPrintable(check("default")));
    }
};

QTEST_MAIN(TestChatWidgetSkin)
#include "test_chatwidget.moc"
