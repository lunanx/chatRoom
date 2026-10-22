#include "ChatArea.h"
#include "InputBar.h"

#include <QFile>
#include <QHBoxLayout>
#include <QLabel>
#include <QListView>
#include <QIODevice>
#include <QFrame>
#include <QAbstractItemView>
#include <QStyle>
#include <QSizePolicy>
#include <QToolButton>
#include <QVBoxLayout>
#include <QWidget>
#include <QDebug>

namespace
{
    constexpr int kHeaderHeight = 48;
    constexpr int kAvatarSize = 32;
    constexpr int kWindowButtonWidth = 32;
    constexpr int kWindowButtonHeight = 26;
}

ChatArea::ChatArea(QWidget *parent)
    : QWidget(parent)
{
    setObjectName(QStringLiteral("chatArea"));
    setAttribute(Qt::WA_StyledBackground, true);

    // ChatArea 样式包含聊天面板背景、Header、消息列表和窗口控制按钮。
    QFile styleFile(QStringLiteral(":/styles/ChatArea.qss"));
    if (styleFile.open(QIODevice::ReadOnly | QIODevice::Text))
    {
        setStyleSheet(QString::fromUtf8(styleFile.readAll()));
    }
    else
    {
        qWarning() << "无法加载 ChatArea QSS：:/styles/ChatArea.qss";
    }

    auto *rootLayout = new QVBoxLayout(this);
    rootLayout->setContentsMargins(0, 0, 0, 0);
    rootLayout->setSpacing(0);

    // Header 固定为 48px，与 HTML 中 .chat-header 的高度一致。
    auto *header = new QWidget(this);
    header->setObjectName(QStringLiteral("chatHeader"));
    header->setFixedHeight(kHeaderHeight);

    auto *headerLayout = new QHBoxLayout(header);
    headerLayout->setContentsMargins(18, 0, 10, 0);
    headerLayout->setSpacing(9);

    // 头像先用首字占位，后续可用真实用户头像的 QPixmap 替换。
    m_avatarLabel = new QLabel(QStringLiteral("林"), header);
    m_avatarLabel->setObjectName(QStringLiteral("chatHeaderAvatar"));
    m_avatarLabel->setAlignment(Qt::AlignCenter);
    m_avatarLabel->setFixedSize(kAvatarSize, kAvatarSize);
    headerLayout->addWidget(m_avatarLabel);

    // 名字和在线状态上下排列，保持 Header 紧凑。
    auto *identityWidget = new QWidget(header);
    identityWidget->setObjectName(QStringLiteral("chatIdentity"));
    auto *identityLayout = new QVBoxLayout(identityWidget);
    identityLayout->setContentsMargins(0, 0, 0, 0);
    identityLayout->setSpacing(1);

    m_nameLabel = new QLabel(QStringLiteral("林间晚风"), identityWidget);
    m_nameLabel->setObjectName(QStringLiteral("chatHeaderName"));

    auto *statusRow = new QWidget(identityWidget);
    statusRow->setObjectName(QStringLiteral("chatStatusRow"));
    auto *statusLayout = new QHBoxLayout(statusRow);
    statusLayout->setContentsMargins(0, 0, 0, 0);
    statusLayout->setSpacing(5);

    m_statusDot = new QLabel(statusRow);
    m_statusDot->setObjectName(QStringLiteral("onlineStatusDot"));
    m_statusDot->setFixedSize(6, 6);
    m_statusLabel = new QLabel(QStringLiteral("在线"), statusRow);
    m_statusLabel->setObjectName(QStringLiteral("onlineStatusText"));
    statusLayout->addWidget(m_statusDot);
    statusLayout->addWidget(m_statusLabel);
    statusLayout->addStretch(1);

    identityLayout->addWidget(m_nameLabel);
    identityLayout->addWidget(statusRow);
    headerLayout->addWidget(identityWidget);
    headerLayout->addStretch(1);

    // 窗口控制按钮位于聊天区右上角。因为主窗口是无边框窗口，按钮需自行控制顶层窗口。
    m_minimizeButton = new QToolButton(header);
    m_minimizeButton->setObjectName(QStringLiteral("minimizeButton"));
    m_minimizeButton->setText(QStringLiteral("—"));
    m_minimizeButton->setToolTip(QStringLiteral("最小化"));

    m_maximizeButton = new QToolButton(header);
    m_maximizeButton->setObjectName(QStringLiteral("maximizeButton"));
    m_maximizeButton->setText(QStringLiteral("□"));
    m_maximizeButton->setToolTip(QStringLiteral("最大化 / 还原"));

    m_closeButton = new QToolButton(header);
    m_closeButton->setObjectName(QStringLiteral("closeButton"));
    m_closeButton->setText(QStringLiteral("×"));
    m_closeButton->setToolTip(QStringLiteral("关闭"));

    for (QToolButton *button : {m_minimizeButton, m_maximizeButton, m_closeButton})
    {
        button->setFixedSize(kWindowButtonWidth, kWindowButtonHeight);
        button->setCursor(Qt::PointingHandCursor);
        button->setFocusPolicy(Qt::NoFocus);
        headerLayout->addWidget(button);
    }
    rootLayout->addWidget(header);

    // 中间消息区使用 QListView。下一阶段会为它配置消息模型和气泡 Delegate。
    m_messageListView = new QListView(this);
    m_messageListView->setObjectName(QStringLiteral("messageListView"));
    m_messageListView->setFrameShape(QFrame::NoFrame);
    m_messageListView->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_messageListView->setSelectionMode(QAbstractItemView::NoSelection);
    m_messageListView->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    m_messageListView->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_messageListView->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    rootLayout->addWidget(m_messageListView, 1);

    // 输入区固定在底部；它向外发出文本信号，ChatArea 再转发给上层。
    m_inputBar = new InputBar(this);
    rootLayout->addWidget(m_inputBar);

    connect(m_minimizeButton, &QToolButton::clicked,
            this, &ChatArea::minimizeWindow);
    connect(m_maximizeButton, &QToolButton::clicked,
            this, &ChatArea::toggleMaximizeWindow);
    connect(m_closeButton, &QToolButton::clicked,
            this, &ChatArea::closeWindow);
    connect(m_inputBar, &InputBar::sendRequested,
            this, &ChatArea::sendRequested);
}

void ChatArea::setConversationInfo(const QString &name,
                                   const QString &avatarText,
                                   bool online)
{
    m_nameLabel->setText(name);
    m_avatarLabel->setText(avatarText.isEmpty() ? QStringLiteral("?") : avatarText.left(1));
    m_statusLabel->setText(online ? QStringLiteral("在线") : QStringLiteral("离线"));

    // 动态属性由 QSS 控制在线绿点和离线灰点的颜色。
    m_statusDot->setProperty("online", online);
    m_statusDot->style()->unpolish(m_statusDot);
    m_statusDot->style()->polish(m_statusDot);
    m_statusDot->update();
}

QListView *ChatArea::messageListView() const
{
    return m_messageListView;
}

InputBar *ChatArea::inputBar() const
{
    return m_inputBar;
}

void ChatArea::minimizeWindow()
{
    QWidget *topLevel = window();
    if (topLevel != nullptr)
    {
        topLevel->showMinimized();
    }
}

void ChatArea::toggleMaximizeWindow()
{
    QWidget *topLevel = window();
    if (topLevel == nullptr)
    {
        return;
    }

    // 最大化后再次点击恢复普通窗口状态。
    if (topLevel->isMaximized())
    {
        topLevel->showNormal();
        m_maximizeButton->setText(QStringLiteral("□"));
    }
    else
    {
        topLevel->showMaximized();
        m_maximizeButton->setText(QStringLiteral("❐"));
    }
}

void ChatArea::closeWindow()
{
    // 关闭的是顶层窗口，不会只把聊天区域从布局中隐藏。
    QWidget *topLevel = window();
    if (topLevel != nullptr)
    {
        topLevel->close();
    }
}
