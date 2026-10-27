#include "ChatArea.h"
#include "InputBar.h"
#include "MessageListModel.h"
#include "MessageFilterProxyModel.h"
#include "MessageItemDelegate.h"

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
#include <QTimer>
#include <QScrollBar>
#include <QPersistentModelIndex>

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

    // 服务器连接状态单独显示，不与当前会话类型（私聊/群聊）混在一起。
    m_networkStatusBadge = new QWidget(header);
    m_networkStatusBadge->setObjectName(QStringLiteral("networkStatusBadge"));
    auto *networkStatusLayout = new QHBoxLayout(m_networkStatusBadge);
    networkStatusLayout->setContentsMargins(9, 5, 9, 5);
    networkStatusLayout->setSpacing(6);

    m_networkStatusDot = new QLabel(m_networkStatusBadge);
    m_networkStatusDot->setObjectName(QStringLiteral("networkStatusDot"));
    m_networkStatusDot->setFixedSize(6, 6);

    m_networkStatusLabel = new QLabel(QStringLiteral("已连接"), m_networkStatusBadge);
    m_networkStatusLabel->setObjectName(QStringLiteral("networkStatusText"));
    networkStatusLayout->addWidget(m_networkStatusDot);
    networkStatusLayout->addWidget(m_networkStatusLabel);
    headerLayout->addWidget(m_networkStatusBadge);
    // 为主窗口右上角的自定义窗口控制按钮留出空间，避免和状态徽标重叠。
    headerLayout->addSpacing(100);
    setNetworkStatus(QStringLiteral("已连接服务器"), QStringLiteral("connected"));

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
    m_messageListView->setUniformItemSizes(false);
    m_messageListView->setSpacing(0);
    m_messageListView->setFlow(QListView::TopToBottom);
    m_messageListView->setWrapping(false);
    m_messageListView->setMouseTracking(true);
    m_messageListView->viewport()->setMouseTracking(true);

    // 消息列表使用 Qt 模型/视图结构：模型保存数据，代理模型按当前会话过滤，
    // Delegate 负责绘制气泡。不会为每条消息创建独立 QWidget。
    m_messageModel = new MessageListModel(this);
    m_messageFilterModel = new MessageFilterProxyModel(this);
    m_messageFilterModel->setSourceModel(m_messageModel);
    m_messageListView->setModel(m_messageFilterModel);
    m_messageDelegate = new MessageItemDelegate(m_messageListView);
    m_messageListView->setItemDelegate(m_messageDelegate);

    // 失败消息会在元信息处提示“点击重试”。QListView 的 clicked 信号比
    // Delegate editorEvent 更适合 NoEditTriggers 的只读消息列表。
    connect(m_messageListView, &QListView::clicked, this, [this](const QModelIndex &index)
    {
        if (index.data(MessageListModel::SendStateRole).toInt() ==
            static_cast<int>(MessageSendState::Failed))
        {
            const QString clientMessageId =
                index.data(MessageListModel::ClientMessageIdRole).toString();
            if (!clientMessageId.isEmpty())
            {
                emit retryMessageRequested(clientMessageId);
            }
        }
    });
    connect(m_messageListView, &QListView::entered, this, [this](const QModelIndex &index)
    {
        const bool retryable = index.data(MessageListModel::SendStateRole).toInt() ==
                               static_cast<int>(MessageSendState::Failed);
        m_messageListView->viewport()->setCursor(
            retryable ? Qt::PointingHandCursor : Qt::ArrowCursor);
    });
    connect(m_messageListView, &QListView::viewportEntered, this, [this]()
    {
        m_messageListView->viewport()->setCursor(Qt::ArrowCursor);
    });

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
    // 历史按钮放在输入区工具栏，但仍由 ChatArea 决定当前会话并向控制器发请求。
    connect(m_inputBar, &InputBar::historyRequested, this, [this]()
    {
        if (m_currentConversationId != 0)
        {
            emit historyRequested(m_currentConversationId);
        }
        else
        {
            setMessageStatus(QStringLiteral("请先选择一个真实会话，再加载历史消息"), true);
        }
    });
}

void ChatArea::setNetworkStatus(const QString &message, const QString &state)
{
    if (m_networkStatusLabel == nullptr || m_networkStatusDot == nullptr ||
        m_networkStatusBadge == nullptr)
    {
        return;
    }

    QString compactText = message;
    if (state == QStringLiteral("connected"))
    {
        compactText = QStringLiteral("已连接");
    }
    else if (state == QStringLiteral("connecting"))
    {
        compactText = QStringLiteral("连接中…");
    }
    else if (state == QStringLiteral("disconnected"))
    {
        compactText = QStringLiteral("已断开");
    }
    else if (state == QStringLiteral("error"))
    {
        compactText = QStringLiteral("连接异常");
    }

    m_networkStatusLabel->setText(compactText);
    m_networkStatusBadge->setProperty("networkState", state);
    m_networkStatusDot->setProperty("networkState", state);
    m_networkStatusLabel->setProperty("networkState", state);

    QWidget *statusWidgets[] = {m_networkStatusBadge, m_networkStatusDot, m_networkStatusLabel};
    for (QWidget *widget : statusWidgets)
    {
        widget->style()->unpolish(widget);
        widget->style()->polish(widget);
        widget->update();
    }
}

void ChatArea::setConversationInfo(const QString &name,
                                   const QString &avatarText,
                                   const QString &statusText)
{
    m_nameLabel->setText(name);
    m_avatarLabel->setText(avatarText.isEmpty() ? QStringLiteral("?") : avatarText.left(1));
    m_statusLabel->setText(statusText);

    // 当前服务端尚未提供在线状态：只有明确显示“在线”时才点亮绿点。
    const bool online = statusText == QStringLiteral("在线");
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

void ChatArea::setMessageStatus(const QString &message, bool isError)
{
    if (m_inputBar != nullptr)
    {
        m_inputBar->setStatusMessage(message, isError);
    }
}

void ChatArea::clearMessageInput()
{
    if (m_inputBar != nullptr)
    {
        m_inputBar->clearInput();
    }
}



void ChatArea::setCurrentConversationId(quint64 conversationId)
{
    m_currentConversationId = conversationId;
    m_messageFilterModel->setConversationId(conversationId);

    // 切换会话时恢复历史按钮状态；ID 为 0 时禁止查询历史消息。
    if (m_inputBar != nullptr)
    {
        m_inputBar->setHistoryLoading(false, true, conversationId != 0);
    }

    // 会话切换后定位到最新一条消息；首次切换或空会话时不会访问无效索引。
    QTimer::singleShot(0, this, [this]()
    {
        const int lastRow = m_messageFilterModel->rowCount() - 1;
        if (lastRow >= 0)
        {
            m_messageListView->scrollTo(
                m_messageFilterModel->index(lastRow, 0),
                QAbstractItemView::PositionAtBottom);
        }
    });
}

void ChatArea::setCurrentUsername(const QString &username)
{
    m_messageModel->setCurrentUsername(username);
    m_messageListView->viewport()->update();
}

void ChatArea::clearMessages()
{
    if (m_messageModel != nullptr)
    {
        m_messageModel->clearMessages();
    }
    setHistoryLoading(false, true);
}

bool ChatArea::appendMessage(const ChatMessageRecord &message)
{
    // 在插入前判断是否贴近底部；如果用户正在向上阅读，收到他人消息时不强行跳到底部。
    // 自己刚发送的 Pending 气泡则始终滚到末尾，避免发送后看不到刚发出的消息。
    const bool shouldFollow = message.conversationId == m_currentConversationId &&
        (message.sendState == MessageSendState::Pending || isNearBottom());
    const quint64 conversationId = message.conversationId;
    const bool inserted = m_messageModel != nullptr && m_messageModel->appendMessage(message);

    if (inserted && shouldFollow)
    {
        QTimer::singleShot(0, this, [this, conversationId]()
        {
            if (m_currentConversationId == conversationId)
            {
                scrollToBottom();
            }
        });
    }
    return inserted;
}

int ChatArea::appendHistoryMessages(quint64 conversationId,
                                    const QVector<ChatMessageRecord> &messages)
{
    if (m_messageModel == nullptr || conversationId == 0 || messages.isEmpty())
    {
        return 0;
    }

    const bool affectsCurrentView = conversationId == m_currentConversationId;
    const int previousVisibleRows = affectsCurrentView ? m_messageFilterModel->rowCount() : 0;
    const bool wasNearBottom = affectsCurrentView && isNearBottom();
    QScrollBar *scrollBar = m_messageListView->verticalScrollBar();
    const int oldScrollValue = scrollBar != nullptr ? scrollBar->value() : 0;

    // 当用户不在底部时，记录最上方可见消息及它相对 viewport 的 Y 坐标。
    // 历史消息插入到它前面后，用这个锚点把视口恢复到原来的内容位置。
    QPersistentModelIndex anchorIndex;
    int anchorTop = 0;
    if (affectsCurrentView && previousVisibleRows > 0 && !wasNearBottom)
    {
        const QRect viewportRect = m_messageListView->viewport()->rect();
        for (int row = 0; row < previousVisibleRows; ++row)
        {
            const QModelIndex candidate = m_messageFilterModel->index(row, 0);
            const QRect itemRect = m_messageListView->visualRect(candidate);
            if (itemRect.isValid() && itemRect.intersects(viewportRect))
            {
                anchorIndex = QPersistentModelIndex(candidate);
                anchorTop = itemRect.top();
                break;
            }
        }
    }

    int insertedCount = 0;
    for (const ChatMessageRecord &message : messages)
    {
        // 防御性校验：批量历史数据不能误插入到其他会话。
        if (message.conversationId != conversationId)
        {
            continue;
        }
        if (m_messageModel->appendMessage(message))
        {
            ++insertedCount;
        }
    }

    if (insertedCount == 0 || !affectsCurrentView)
    {
        return insertedCount;
    }

    QTimer::singleShot(0, this,
        [this, conversationId, previousVisibleRows, wasNearBottom,
         anchorIndex, anchorTop, oldScrollValue]()
        {
            if (m_currentConversationId != conversationId)
            {
                return;
            }

            // 首次加载历史时，直接定位到最新一条；若原本在底部，则继续保持底部。
            if (previousVisibleRows == 0 || wasNearBottom)
            {
                scrollToBottom();
                return;
            }

            QScrollBar *bar = m_messageListView->verticalScrollBar();
            if (bar == nullptr)
            {
                return;
            }

            if (anchorIndex.isValid())
            {
                const QRect restoredRect = m_messageListView->visualRect(QModelIndex(anchorIndex));
                if (restoredRect.isValid())
                {
                    // itemRect.top() 是 viewport 坐标，差值即需要补偿的滚动像素。
                    bar->setValue(bar->value() + restoredRect.top() - anchorTop);
                    return;
                }
            }

            // 极少数情况下锚点因过滤/重排失效，退回原滚动条数值。
            bar->setValue(oldScrollValue);
        });

    return insertedCount;
}

bool ChatArea::setMessageSendState(const QString &clientMessageId, MessageSendState state)
{
    return m_messageModel != nullptr &&
           m_messageModel->setSendState(clientMessageId, state);
}

bool ChatArea::confirmPendingMessage(const QString &clientMessageId,
                                     quint64 serverMessageId,
                                     quint64 sequence)
{
    const bool wasNearBottom = isNearBottom();
    QScrollBar *scrollBar = m_messageListView->verticalScrollBar();
    const int oldScrollValue = scrollBar != nullptr ? scrollBar->value() : 0;
    QPersistentModelIndex anchorIndex;
    int anchorTop = 0;

    // 确认响应可能令 Pending 消息按服务端 seq 重新排序。
    // 用户若已向上翻阅，不应因为 ACK 到达而跳到最底部。
    if (!wasNearBottom)
    {
        const QRect viewportRect = m_messageListView->viewport()->rect();
        for (int row = 0; row < m_messageFilterModel->rowCount(); ++row)
        {
            const QModelIndex candidate = m_messageFilterModel->index(row, 0);
            const QRect itemRect = m_messageListView->visualRect(candidate);
            if (itemRect.isValid() && itemRect.intersects(viewportRect))
            {
                anchorIndex = QPersistentModelIndex(candidate);
                anchorTop = itemRect.top();
                break;
            }
        }
    }

    const bool confirmed = m_messageModel != nullptr &&
        m_messageModel->confirmPendingMessage(clientMessageId, serverMessageId, sequence);
    if (confirmed)
    {
        QTimer::singleShot(0, this,
            [this, wasNearBottom, anchorIndex, anchorTop, oldScrollValue]()
            {
                if (m_currentConversationId == 0)
                {
                    return;
                }
                if (wasNearBottom)
                {
                    scrollToBottom();
                    return;
                }

                QScrollBar *bar = m_messageListView->verticalScrollBar();
                if (bar == nullptr)
                {
                    return;
                }
                if (anchorIndex.isValid())
                {
                    const QRect rect = m_messageListView->visualRect(QModelIndex(anchorIndex));
                    if (rect.isValid())
                    {
                        bar->setValue(bar->value() + rect.top() - anchorTop);
                        return;
                    }
                }
                bar->setValue(oldScrollValue);
            });
    }
    return confirmed;
}


void ChatArea::setHistoryLoading(bool loading, bool hasMore)
{
    if (m_inputBar != nullptr)
    {
        m_inputBar->setHistoryLoading(loading, hasMore, m_currentConversationId != 0);
    }
}

bool ChatArea::isNearBottom() const
{
    if (m_messageListView == nullptr)
    {
        return true;
    }
    const QScrollBar *bar = m_messageListView->verticalScrollBar();
    if (bar == nullptr)
    {
        return true;
    }

    // 24px 容差用于吸收气泡重新布局和字体度量带来的轻微滚动误差。
    return bar->maximum() - bar->value() <= 24;
}

void ChatArea::scrollToBottom()
{
    if (m_messageListView == nullptr || m_messageFilterModel == nullptr)
    {
        return;
    }
    const int lastRow = m_messageFilterModel->rowCount() - 1;
    if (lastRow >= 0)
    {
        m_messageListView->scrollTo(
            m_messageFilterModel->index(lastRow, 0),
            QAbstractItemView::PositionAtBottom);
    }
}
