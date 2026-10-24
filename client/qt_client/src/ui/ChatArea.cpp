#include "ChatArea.h"
#include "InputBar.h"

#include <QFile>
#include <QHBoxLayout>
#include <QLabel>
#include <QListView>
#include <QIODevice>
#include <QSplitter>
#include <QResizeEvent>
#include <QFrame>
#include <QAbstractItemView>
#include <QStyle>
#include <QSizePolicy>
#include <QVBoxLayout>
#include <QWidget>
#include <QDebug>

namespace
{
    constexpr int kHeaderHeight = 48;
    constexpr int kAvatarSize = 32;
    constexpr int kInputBarInitialHeight = 148;
    constexpr int kInputBarMinimumHeight = 112;
}

ChatArea::ChatArea(QWidget *parent)
    : QWidget(parent)
{
    setObjectName(QStringLiteral("chatArea"));
    setAttribute(Qt::WA_StyledBackground, true);

    // ChatArea 样式只负责聊天面板背景、Header 和消息列表；窗口控制按钮由 MainWindow 统一管理。
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

    // 不在 ChatArea 的会话 Header 中创建另一组窗口控制按钮。
    // 顶层窗口的控制按钮统一由 MainWindow 中的 WindowTopControls 提供。
    rootLayout->addWidget(header);

    // 使用垂直 QSplitter 分隔消息区和输入区，用户可以拖动中间分隔条调整比例。
    // 输入区默认较矮；后续 resizeEvent 还会限制它最多占可用高度的一半。
    m_contentSplitter = new QSplitter(Qt::Vertical, this);
    m_contentSplitter->setObjectName(QStringLiteral("chatContentSplitter"));
    m_contentSplitter->setChildrenCollapsible(false);
    // 消息显示区与输入区之间保留 0px 的细间距，避免分隔条显得过厚。
    m_contentSplitter->setHandleWidth(0);
    m_contentSplitter->setOpaqueResize(true);

    // 中间消息区使用 QListView。下一阶段会为它配置消息模型和气泡 Delegate。
    m_messageListView = new QListView(m_contentSplitter);
    m_messageListView->setObjectName(QStringLiteral("messageListView"));
    m_messageListView->setFrameShape(QFrame::NoFrame);
    m_messageListView->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_messageListView->setSelectionMode(QAbstractItemView::NoSelection);
    m_messageListView->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    m_messageListView->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_messageListView->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);

    // 输入区放在 QSplitter 的下半部分，初始高度约 148px，比之前更紧凑。
    m_inputBar = new InputBar(m_contentSplitter);
    m_inputBar->setMinimumHeight(kInputBarMinimumHeight);
    m_inputBar->setMaximumHeight(kInputBarInitialHeight);

    m_contentSplitter->addWidget(m_messageListView);
    m_contentSplitter->addWidget(m_inputBar);
    m_contentSplitter->setStretchFactor(0, 1); // 消息显示区优先获得剩余空间。
    m_contentSplitter->setStretchFactor(1, 0); // 输入区默认不主动扩张。
    m_contentSplitter->setSizes({500, kInputBarInitialHeight});
    rootLayout->addWidget(m_contentSplitter, 1);

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

void ChatArea::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);

    if (m_contentSplitter == nullptr || m_inputBar == nullptr)
    {
        return;
    }

    // 输入区最多占消息区与输入区总高度的一半（1:1）。
    // 当窗口很小时，仍保留最低可用高度，避免编辑框被压得无法使用。
    const int halfAvailableHeight = m_contentSplitter->height() / 2;
    const int allowedInputHeight = qMax(kInputBarMinimumHeight, halfAvailableHeight);
    m_inputBar->setMaximumHeight(allowedInputHeight);

    // 如果窗口缩小导致输入区超过新上限，主动收回超出的部分。
    const QList<int> sizes = m_contentSplitter->sizes();
    if (sizes.size() == 2 && sizes.at(1) > allowedInputHeight)
    {
        m_contentSplitter->setSizes({qMax(0, m_contentSplitter->height() - allowedInputHeight),
                                     allowedInputHeight});
    }
}

QListView *ChatArea::messageListView() const
{
    return m_messageListView;
}

InputBar *ChatArea::inputBar() const
{
    return m_inputBar;
}

