#include "InputBar.h"

#include <QFile>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QTextEdit>
#include <QToolButton>
#include <QVBoxLayout>
#include <QIODevice>
#include <QFrame>
#include <QKeyEvent>
#include <QEvent>
#include <QStringList>
#include <QDebug>

namespace
{
    constexpr int kInputBarMinimumHeight = 170;
    constexpr int kToolButtonSize = 30;
}

InputBar::InputBar(QWidget *parent)
    : QWidget(parent)
{
    setObjectName(QStringLiteral("inputBar"));
    setAttribute(Qt::WA_StyledBackground, true);
    setMinimumHeight(kInputBarMinimumHeight);

    // QSS 从 Qt 资源系统加载，避免依赖程序启动时所在的工作目录。
    QFile styleFile(QStringLiteral(":/styles/InputBar.qss"));
    if (styleFile.open(QIODevice::ReadOnly | QIODevice::Text))
    {
        setStyleSheet(QString::fromUtf8(styleFile.readAll()));
    }
    else
    {
        qWarning() << "无法加载 InputBar QSS：:/styles/InputBar.qss";
    }

    auto *rootLayout = new QVBoxLayout(this);
    rootLayout->setContentsMargins(18, 12, 18, 15);
    rootLayout->setSpacing(8);

    // 工具栏使用轻量的 QToolButton；这里先提供图标和提示文字。
    // 点击动作（表情选择、图片选择等）将在后续功能阶段实现。
    auto *toolbar = new QWidget(this);
    toolbar->setObjectName(QStringLiteral("inputToolbar"));
    auto *toolbarLayout = new QHBoxLayout(toolbar);
    toolbarLayout->setContentsMargins(0, 0, 0, 0);
    toolbarLayout->setSpacing(7);

    const QStringList toolSymbols = {
        QStringLiteral("☺"), // 表情
        QStringLiteral("▧"), // 图片
        QStringLiteral("⌕"), // 文件/附件
        QStringLiteral("✂")  // 截图占位
    };
    const QStringList toolTips = {
        QStringLiteral("表情"),
        QStringLiteral("发送图片"),
        QStringLiteral("发送文件"),
        QStringLiteral("截图")
    };
    const QStringList toolNames = {
        QStringLiteral("emojiToolButton"),
        QStringLiteral("imageToolButton"),
        QStringLiteral("fileToolButton"),
        QStringLiteral("screenshotToolButton")
    };

    for (qsizetype i = 0; i < toolSymbols.size(); ++i)
    {
        auto *toolButton = new QToolButton(toolbar);
        toolButton->setObjectName(toolNames.at(i));
        toolButton->setText(toolSymbols.at(i));
        toolButton->setToolTip(toolTips.at(i));
        toolButton->setFixedSize(kToolButtonSize, kToolButtonSize);
        toolButton->setCursor(Qt::PointingHandCursor);
        toolButton->setFocusPolicy(Qt::NoFocus);
        toolbarLayout->addWidget(toolButton);
    }

    toolbarLayout->addStretch(1);
    auto *shortcutHint = new QLabel(QStringLiteral("Enter 发送 · Shift + Enter 换行"), toolbar);
    shortcutHint->setObjectName(QStringLiteral("inputShortcutHint"));
    toolbarLayout->addWidget(shortcutHint);
    rootLayout->addWidget(toolbar);

    // 编辑框不显示原生边框，外观由父级 InputBar.qss 统一控制。
    m_editor = new QTextEdit(this);
    m_editor->setObjectName(QStringLiteral("messageEditor"));
    m_editor->setFrameShape(QFrame::NoFrame);
    m_editor->setAcceptRichText(false);
    m_editor->setPlaceholderText(QStringLiteral("输入消息…"));
    m_editor->setTabChangesFocus(false);
    m_editor->installEventFilter(this);
    rootLayout->addWidget(m_editor, 1);

    // 底部操作区右对齐发送按钮。
    auto *bottomRow = new QHBoxLayout();
    bottomRow->setContentsMargins(0, 0, 0, 0);
    bottomRow->addStretch(1);

    m_sendButton = new QPushButton(QStringLiteral("发送  ↗"), this);
    m_sendButton->setObjectName(QStringLiteral("sendButton"));
    m_sendButton->setCursor(Qt::PointingHandCursor);
    m_sendButton->setFixedSize(92, 36);
    m_sendButton->setDefault(false);
    bottomRow->addWidget(m_sendButton);
    rootLayout->addLayout(bottomRow);

    connect(m_sendButton, &QPushButton::clicked,
            this, &InputBar::handleSendClicked);
}

void InputBar::clearInput()
{
    m_editor->clear();
}

QString InputBar::inputText() const
{
    return m_editor->toPlainText();
}

bool InputBar::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == m_editor && event->type() == QEvent::KeyPress)
    {
        auto *keyEvent = static_cast<QKeyEvent *>(event);
        // Enter 直接发送；按住 Shift 时不拦截，让文本编辑框插入换行。
        if ((keyEvent->key() == Qt::Key_Return || keyEvent->key() == Qt::Key_Enter)
            && !(keyEvent->modifiers() & Qt::ShiftModifier))
        {
            handleSendClicked();
            return true;
        }
    }

    return QWidget::eventFilter(watched, event);
}

void InputBar::handleSendClicked()
{
    // 暂不允许发送纯空白消息，避免后续网络层处理无意义数据。
    const QString text = m_editor->toPlainText().trimmed();
    if (text.isEmpty())
    {
        return;
    }

    emit sendRequested(text);
    m_editor->clear();
}
