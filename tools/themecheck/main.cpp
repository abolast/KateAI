/*
 * SPDX-FileCopyrightText: 2026 ObiWindu <Obi.wandu@proton.me>
 * SPDX-License-Identifier: LGPL-2.1-or-later
 *
 * Offline harness for the chat skins.  Builds a representative widget tree
 * under the active skin, reports resolved colours and dumps a PNG per skin, so
 * a skin can be checked without launching Kate.
 */

#include "theme.h"
#include "toolcallwidget.h"

#include <QApplication>
#include <QDir>
#include <QFile>
#include <QHBoxLayout>
#include <QImage>
#include <QLabel>
#include <QPainter>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QStandardPaths>
#include <QStyle>
#include <QTextBrowser>
#include <QTextDocument>
#include <QVBoxLayout>
#include <QWidget>

#include <cstdio>

using namespace KateAi;
using namespace Qt::Literals::StringLiterals;

namespace
{

void report(const QString &what, const QWidget *widget)
{
    if (!widget) {
        return;
    }
    fprintf(stderr, "  %-24s window=%-9s base=%-9s text=%s\n", qPrintable(what),
            qPrintable(widget->palette().color(QPalette::Window).name()),
            qPrintable(widget->palette().color(QPalette::Base).name()),
            qPrintable(widget->palette().color(QPalette::Text).name()));
}

/** Prepares a QTextBrowser as ChatWidget does. */
void styleThinkingBrowser(QTextBrowser *browser)
{
    browser->setObjectName(u"thinkingBrowser"_s);
    browser->setReadOnly(true);
    browser->setFrameShape(QFrame::NoFrame);
    browser->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    browser->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    browser->document()->setDefaultStyleSheet(Theme::instance()->documentCss());
    QPalette pal = browser->palette();
    pal.setColor(QPalette::Text, Theme::instance()->color(u"text_muted"_s));
    pal.setColor(QPalette::Base, Qt::transparent);
    browser->setPalette(pal);
}

/** Mirrors ChatWidget::applySkin(). */
void refreshSkin(QWidget *root)
{
    Theme *theme = Theme::instance();
    root->setStyleSheet(theme->widgetsCss());
    const QList<QWidget *> widgets = root->findChildren<QWidget *>();
    for (QWidget *widget : widgets) {
        if (widget) {
            QStyle *style = widget->style();
            style->unpolish(widget);
            style->polish(widget);
            widget->update();
        }
    }
    root->style()->unpolish(root);
    root->style()->polish(root);
    root->update();

    const QString documentCss = theme->documentCss();
    const QList<QTextBrowser *> browsers = root->findChildren<QTextBrowser *>();
    for (QTextBrowser *browser : browsers) {
        if (!browser || !browser->document()) {
            continue;
        }
        browser->setStyleSheet(QString());
        browser->document()->setDefaultStyleSheet(documentCss);
        QPalette pal = browser->palette();
        pal.setColor(QPalette::Text, theme->color(u"text_muted"_s));
        pal.setColor(QPalette::Base, Qt::transparent);
        browser->setPalette(pal);
        const QString markdown = browser->property("kateaiMarkdown").toString();
        if (!markdown.isEmpty()) {
            browser->setMarkdown(markdown);
        }
    }
}

void dumpPixels(QWidget *widget, const QString &what)
{
    const QImage image = widget->grab().toImage();
    QHash<QRgb, int> histogram;
    for (int y = 0; y < image.height(); ++y) {
        for (int x = 0; x < image.width(); ++x) {
            histogram[image.pixel(x, y)]++;
        }
    }
    QList<QPair<int, QRgb>> ranked;
    for (auto it = histogram.constBegin(); it != histogram.constEnd(); ++it) {
        ranked.append({it.value(), it.key()});
    }
    std::sort(ranked.begin(), ranked.end(), [](const auto &a, const auto &b) { return a.first > b.first; });
    QStringList top;
    for (int i = 0; i < qMin(3, int(ranked.size())); ++i) {
        top << QStringLiteral("%1 x%2").arg(QColor(ranked.at(i).second).name()).arg(ranked.at(i).first);
    }
    fprintf(stderr, "  pixels %-20s %s\n", qPrintable(what), qPrintable(top.join(QStringLiteral(", "))));
}

void dump(QWidget *widget, const QString &path)
{
    const QPixmap pixmap = widget->grab();
    const QImage image = pixmap.toImage();
    if (!image.save(path)) {
        fprintf(stderr, "  failed to write %s\n", qPrintable(path));
        return;
    }
    // Report the most frequent colours so the painted text colour is visible.
    QHash<QRgb, int> histogram;
    for (int y = 0; y < image.height(); ++y) {
        for (int x = 0; x < image.width(); ++x) {
            histogram[image.pixel(x, y)]++;
        }
    }
    QList<QPair<int, QRgb>> ranked;
    for (auto it = histogram.constBegin(); it != histogram.constEnd(); ++it) {
        ranked.append({it.value(), it.key()});
    }
    std::sort(ranked.begin(), ranked.end(), [](const auto &a, const auto &b) {
        return a.first > b.first;
    });
    QStringList top;
    for (int i = 0; i < qMin(4, int(ranked.size())); ++i) {
        top << QStringLiteral("%1 x%2").arg(QColor(ranked.at(i).second).name()).arg(ranked.at(i).first);
    }
    fprintf(stderr, "  %s -> %s\n", qPrintable(path), qPrintable(top.join(QStringLiteral(", "))));
}

} // namespace

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    // Deliberately NOT setting an application name: this is what the plugin sees
    // when it runs inside Kate, where the application name is "kate".
    fprintf(stderr, "[themecheck] applicationName=%s organizationName=%s\n",
            qPrintable(QCoreApplication::applicationName()),
            qPrintable(QCoreApplication::organizationName()));

    Theme *theme = Theme::instance();
    const QStringList args = app.arguments();
    const QString requested = args.size() > 1 && !args.at(1).startsWith(u'-') ? args.at(1)
                                                                             : QStringLiteral("default");
    const bool quiet = args.contains(u"--quiet"_s);
    theme->load(requested);
    // Skin location, so a theme can be dropped in the right folder.
    fprintf(stderr, "[themecheck] skins: %s\n", qPrintable(Theme::userThemeDir()));
    if (!quiet) {
        fprintf(stderr, "[themecheck] theme=%s tokens=%lld\n", qPrintable(theme->name()),
                (long long)theme->tokenNames().size());
    }

    QWidget root;
    root.setObjectName(u"ChatWidget"_s);
    auto *rootLayout = new QVBoxLayout(&root);
    rootLayout->setContentsMargins(0, 0, 0, 0);
    rootLayout->setSpacing(0);

    auto *toolbar = new QWidget(&root);
    toolbar->setObjectName(u"chatToolbar"_s);
    auto *toolbarLayout = new QHBoxLayout(toolbar);
    auto *modelSelector = new QPushButton(u"DeepSeek: deepseek-flash"_s, toolbar);
    modelSelector->setObjectName(u"modelSelector"_s);
    toolbarLayout->addWidget(modelSelector);
    auto *threadTitle = new QLabel(u"New Thread"_s, toolbar);
    threadTitle->setObjectName(u"threadTitle"_s);
    toolbarLayout->addWidget(threadTitle);
    rootLayout->addWidget(toolbar);

    auto *scroll = new QScrollArea(&root);
    scroll->setObjectName(u"transcriptScroll"_s);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    auto *container = new QWidget(scroll);
    container->setObjectName(u"transcriptContainer"_s);
    auto *containerLayout = new QVBoxLayout(container);

    auto *card = new QWidget(container);
    card->setObjectName(u"userMessageCard"_s);
    auto *cardLayout = new QVBoxLayout(card);
    auto *header = new QLabel(u"YOU"_s, card);
    header->setObjectName(u"userMessageHeader"_s);
    cardLayout->addWidget(header);
    auto *body = new QLabel(u"Show me the theme switch."_s, card);
    body->setObjectName(u"userMessageBody"_s);
    cardLayout->addWidget(body);
    containerLayout->addWidget(card);

    // Assistant turn with a reasoning block, exactly like ChatWidget builds it.
    auto *assistant = new QWidget(container);
    auto *assistantLayout = new QVBoxLayout(assistant);
    auto *aiHeader = new QLabel(u"KATE AI"_s, assistant);
    aiHeader->setObjectName(u"assistantHeader"_s);
    assistantLayout->addWidget(aiHeader);

    auto *thinkingBlock = new QWidget(assistant);
    auto *thinkingLayout = new QVBoxLayout(thinkingBlock);
    auto *thinkingToggle = new QPushButton(u"\u25b4 Reasoning"_s, thinkingBlock);
    thinkingToggle->setObjectName(u"thinkingToggle"_s);
    thinkingToggle->setFlat(true);
    thinkingLayout->addWidget(thinkingToggle);
    auto *thinkingBrowser = new QTextBrowser(thinkingBlock);
    styleThinkingBrowser(thinkingBrowser);
    thinkingBrowser->setProperty("kateaiMarkdown", u"The user wants me to inspect the thinking block colour."_s);
    thinkingBrowser->setMarkdown(u"The user wants me to inspect the thinking block colour."_s);
    thinkingBrowser->setFixedHeight(60);
    thinkingLayout->addWidget(thinkingBrowser);
    assistantLayout->addWidget(thinkingBlock);

    // Bash tool card, expanded, with command and output.
    auto *bashCard = new ToolCallWidget(u"call-bash"_s, container);
    bashCard->setToolInfo(u"bash"_s, u"grep -rn QPalette::Text src/ | head -5"_s, ToolRisk::Execute);
    bashCard->setPreviewText(QStringLiteral("src/chatwidget.cpp:706: pal.setColor(QPalette::Text, ...)\n"
                                            "src/chatwidget.cpp:1075: pal.setColor(QPalette::Text, ...)\n"
                                            "2 matches in 1 file"));
    ToolResult bashResult;
    bashResult.toolCallId = u"call-bash"_s;
    bashResult.name = u"bash"_s;
    bashResult.output = QStringLiteral("exit code: 0\nelapsed: 0.4s");
    bashResult.ok = true;
    bashCard->setFinished(bashResult);
    bashCard->setExpanded(true);
    bashCard->setFixedHeight(150);
    containerLayout->addWidget(bashCard);

    auto *answer = new QTextBrowser(assistant);
    answer->setObjectName(u"assistantBrowser"_s);
    answer->setFrameShape(QFrame::NoFrame);
    answer->document()->setDefaultStyleSheet(theme->documentCss());
    answer->setProperty("kateaiMarkdown", u"Here is a `code` sample and a code block:\n\n```cpp\nint main() { return 0; }\n```\n"_s);
    answer->setMarkdown(answer->property("kateaiMarkdown").toString());
    answer->setFixedHeight(120);
    assistantLayout->addWidget(answer);

    containerLayout->addWidget(assistant);
    containerLayout->addStretch();
    scroll->setWidget(container);
    rootLayout->addWidget(scroll, 1);

    auto *composer = new QWidget(&root);
    composer->setObjectName(u"composerContainer"_s);
    auto *composerLayout = new QHBoxLayout(composer);
    auto *prompt = new QPlainTextEdit(composer);
    prompt->setObjectName(u"promptEdit"_s);
    prompt->setPlainText(u"Explain the diff."_s);
    prompt->setFixedHeight(56);
    composerLayout->addWidget(prompt);
    auto *send = new QPushButton(u"\u25b2"_s, composer);
    send->setObjectName(u"sendButton"_s);
    send->setProperty("mode", u"send"_s);
    composerLayout->addWidget(send);
    rootLayout->addWidget(composer);

    // "--fresh": apply the skin after building the tree (startup path).
    // "--live": build and show first, then restyle (theme switch path), which is
    // where palette/QSS interactions show up.
    const bool live = args.contains(u"--live"_s);
    if (!live) {
        // Startup path: ChatWidget applies the sheet in its constructor.
        refreshSkin(&root);
    } else {
        // Live path: apply the skin, then listen for switches as ChatWidget does.
        refreshSkin(&root);
        QObject::connect(theme, &Theme::themeChanged, &root, [&root]() { refreshSkin(&root); });
    }
    root.resize(560, 700);
    root.show();
    app.processEvents();
    if (live) {
        fprintf(stderr, "[themecheck] before switch: theme=%s tokens=%lld cssLen=%d accent=%s thinking text=%s\n",
                qPrintable(theme->name()), (long long)theme->tokenNames().size(),
                int(theme->widgetsCss().size()), qPrintable(theme->token(u"accent"_s)),
                qPrintable(thinkingBrowser->palette().color(QPalette::Text).name()));
        theme->load(theme->name() == u"default"_s ? u"kate-ayu-mirage"_s : u"default"_s);
        app.processEvents();
        fprintf(stderr, "[themecheck] after switch: theme=%s tokens=%lld cssLen=%d accent=%s bg=%s thinking text=%s\n",
                qPrintable(theme->name()), (long long)theme->tokenNames().size(),
                int(theme->widgetsCss().size()), qPrintable(theme->token(u"accent"_s)),
                qPrintable(theme->token(u"bg_base"_s)),
                qPrintable(thinkingBrowser->palette().color(QPalette::Text).name()));
    }

    if (!quiet) {
        report(u"scroll viewport"_s, scroll->viewport());
        report(u"user message card"_s, card);
        report(u"thinking browser"_s, thinkingBrowser);
        report(u"assistant browser"_s, answer);
        report(u"composer container"_s, composer);
        report(u"send button"_s, send);
        fprintf(stderr, "  text_muted token=%s  documentCss color=%s\n",
                qPrintable(theme->token(u"text_muted"_s)),
                qPrintable(answer->document()->defaultStyleSheet().section(u"color:"_s, 1, 1).section(u';', 0, 0).trimmed()));
        dumpPixels(thinkingBrowser, u"thinking(text)"_s);
        dumpPixels(answer, u"assistant(markdown)"_s);
        dump(&root, QDir::currentPath() + QStringLiteral("/build-abola/themecheck-%1.png").arg(theme->name()));
    }

    // Live check: write a skin that only overrides bg_base into the user theme
    // directory, reload it and confirm the running widget tree follows.
    if (args.contains(u"--live-check"_s)) {
        const QString dirPath = qEnvironmentVariable("KATEAI_THEME_DIR", Theme::userThemeDir());
        QDir dir(dirPath);
        if (!dir.exists() && !dir.mkpath(u"."_s)) {
            fprintf(stderr, "  live-check: cannot create %s\n", qPrintable(dir.path()));
            return 1;
        }
        QFile skin(dir.filePath(u"probe.theme"_s));
        if (!skin.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            fprintf(stderr, "  live-check: cannot write %s\n", qPrintable(skin.fileName()));
            return 1;
        }
        skin.write("@bg_base = #123456\n@accent = #654321\n");
        skin.close();

        QObject::connect(theme, &Theme::themeChanged, &root, [&root]() { refreshSkin(&root); });
        theme->load(u"probe"_s);
        app.processEvents();

        const QColor live = scroll->viewport()->palette().color(QPalette::Window);
        fprintf(stderr, "  live-check: theme=%s bg_base=%s viewport=%s accent=%s\n",
                qPrintable(theme->name()), qPrintable(theme->token(u"bg_base"_s)),
                qPrintable(live.name()), qPrintable(theme->token(u"accent"_s)));
        dump(&root, QDir::currentPath() + QStringLiteral("/build-abola/themecheck-live.png"));

        dir.remove(u"probe.theme"_s);
        theme->load(u"default"_s);
        return live == QColor(u"#123456"_s) ? 0 : 1;
    }

    return 0;
}
