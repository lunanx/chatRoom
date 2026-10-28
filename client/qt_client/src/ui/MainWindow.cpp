#include "MainWindow.h"
#include "WindowTopControls.h"

#include "Theme.h"
#include "ui/ChatArea.h"
#include "ui/ContactsPanel.h"
#include "ui/SessionListPanel.h"
#include "ui/SideBar.h"
#include "model/ChatMessageRecord.h"

#include <QEvent>
#include <QFont>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QMouseEvent>
#include <QResizeEvent>
#include <QPainter>
#include <QCursor>
#include <QApplication>
#include <QChildEvent>
#include <QRadialGradient>
#include <QSplitter>
#include <QStackedWidget>
#include <QVBoxLayout>
#include <QWidget>

// 初始化无边框主窗口和工作台页面。
MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent),
      m_pageStack(new QStackedWidget(this))
{
    setWindowTitle(QStringLiteral("Go聊"));
    setWindowFlags(Qt::Window | Qt::FramelessWindowHint);
    // 使用不透明、直角的无边框窗口，不应用圆角遮罩。
    // 无系统边框，但保留自定义边缘缩放逻辑；最小尺寸防止布局被压坏。
    setMinimumSize(Theme::WindowMinimumWidth, Theme::WindowMinimumHeight);
    resize(1280, 800);

    // 页面栈背景保持透明，让 paintEvent 统一绘制窗口的紫灰渐变光晕。
    m_pageStack->setObjectName(QStringLiteral("pageStack"));
    m_pageStack->installEventFilter(this);
    m_pageStack->setFrameShape(QFrame::NoFrame);
    m_pageStack->setStyleSheet(QStringLiteral(
        "QStackedWidget#pageStack { background: transparent; border: none; }"));

    // 创建原来的三栏聊天工作台。
    QWidget *chatPage = createChatWorkspacePage();

    // 保留原来的无边框窗口缩放功能。
    installResizeEventFilters(chatPage);

    // 页面栈中只放聊天工作台。
    m_pageStack->addWidget(chatPage);
    m_pageStack->setCurrentWidget(chatPage);

    setCentralWidget(m_pageStack);

    // 主窗口也使用与页面背景融合的同色系圆点控制按钮。
    m_windowControls = createWindowTopControls(this, this);
    m_windowControls->raise();

    // 主界面尺寸：1200 × 800。
    resize(1200, 800);
    m_windowControls->move(width() - 18 - m_windowControls->width(), 14);
    m_windowControls->raise();
}

// 组合工作台：左侧导航，右侧按需显示消息、联系人或功能占位页。
QWidget *MainWindow::createChatWorkspacePage()
{
    auto *page = new QWidget;
    page->setObjectName(QStringLiteral("chatWorkspacePage"));
    page->setAttribute(Qt::WA_TranslucentBackground);
    page->setStyleSheet(QStringLiteral("QWidget#chatWorkspacePage { background: transparent; }"));

    auto *rootLayout = new QHBoxLayout(page);
    rootLayout->setContentsMargins(0, 0, 0, 0);
    rootLayout->setSpacing(0);

    // 左侧导航栏保持常驻；右侧用页面栈切换消息区、联系人和暂未开放的区域。
    m_sideBar = new SideBar(page);
    SideBar *sideBar = m_sideBar;
    sideBar->setFixedWidth(Theme::NavigationWidth);
    connect(sideBar, &SideBar::logoutRequested,
            this, &MainWindow::logoutRequested);

    m_workspaceStack = new QStackedWidget(page);
    m_workspaceStack->setObjectName(QStringLiteral("workspaceStack"));
    m_workspaceStack->setStyleSheet(QStringLiteral(
        "QStackedWidget#workspaceStack { background: transparent; border: none; }"));

    // 消息页内部仍使用可拖动的分隔条调整会话列表与聊天区宽度。
    auto *messagesPage = new QWidget(m_workspaceStack);
    messagesPage->setObjectName(QStringLiteral("messagesPage"));
    auto *messageSplitter = new QSplitter(Qt::Horizontal, messagesPage);
    messageSplitter->setObjectName(QStringLiteral("chatWorkspaceSplitter"));
    messageSplitter->setChildrenCollapsible(false);
    messageSplitter->setHandleWidth(0);
    messageSplitter->setOpaqueResize(true);

    m_sessionPanel = new SessionListPanel(messageSplitter);
    SessionListPanel *sessionPanel = m_sessionPanel;
    sessionPanel->resize(Theme::ConversationWidth, sessionPanel->height());
    connect(sessionPanel, &SessionListPanel::newSessionRequested,
            this, &MainWindow::newSessionRequested);

    m_chatArea = new ChatArea(messageSplitter);
    ChatArea *chatArea = m_chatArea;
    chatArea->setConversationInfo(QStringLiteral("选择会话"), QString(),
                                  QStringLiteral("等待加载会话列表"));

    connect(chatArea, &ChatArea::sendRequested, this,
            [this](const QString &text)
            {
                emit messageSendRequested(text, m_currentConversationId,
                                          m_currentConversationIsGroup);
            });
    connect(chatArea, &ChatArea::historyRequested,
            this, &MainWindow::historyRequested);
    connect(chatArea, &ChatArea::retryMessageRequested,
            this, &MainWindow::messageRetryRequested);
    connect(chatArea, &ChatArea::inviteMembersRequested,
            this, &MainWindow::groupInviteRequested);

    messageSplitter->addWidget(sessionPanel);
    messageSplitter->addWidget(chatArea);
    messageSplitter->setStretchFactor(0, 0);
    messageSplitter->setStretchFactor(1, 1);
    messageSplitter->setSizes({Theme::ConversationWidth, 900});

    auto *messagesLayout = new QHBoxLayout(messagesPage);
    messagesLayout->setContentsMargins(0, 0, 0, 0);
    messagesLayout->setSpacing(0);
    messagesLayout->addWidget(messageSplitter);
    m_workspaceStack->addWidget(messagesPage);

    // 联系人页只负责展示和发出操作请求；网络与数据解析仍归 AppController。
    m_contactsPanel = new ContactsPanel(m_workspaceStack);
    connect(m_contactsPanel, &ContactsPanel::refreshRequested,
            this, &MainWindow::contactsRefreshRequested);
    connect(m_contactsPanel, &ContactsPanel::sendFriendRequestRequested,
            this, &MainWindow::friendRequestSendRequested);
    connect(m_contactsPanel, &ContactsPanel::handleFriendRequestRequested,
            this, &MainWindow::friendRequestHandleRequested);
    connect(m_contactsPanel, &ContactsPanel::openPrivateChatRequested,
            this, [this](quint64 userId)
            {
                // 从联系人直接发消息时，先切回消息工作区；会话创建成功后再选中真实会话。
                if (m_sideBar != nullptr)
                {
                    m_sideBar->setCurrentSection(SideBar::Messages);
                }
                if (m_workspaceStack != nullptr)
                {
                    m_workspaceStack->setCurrentIndex(SideBar::Messages);
                }
                emit contactOpenPrivateChatRequested(userId);
            });
    m_workspaceStack->addWidget(m_contactsPanel);

    // 收藏与文件尚未进入开发阶段，显示明确占位页，避免导航点击后看起来没有反应。
    auto makePlaceholderPage = [this](const QString &titleText, const QString &descriptionText)
    {
        auto *placeholder = new QWidget(m_workspaceStack);
        placeholder->setObjectName(QStringLiteral("featurePlaceholderPage"));
        placeholder->setStyleSheet(QStringLiteral("QWidget#featurePlaceholderPage { background: rgba(252,251,255,238); }"));
        auto *layout = new QVBoxLayout(placeholder);
        layout->setContentsMargins(36, 38, 36, 30);
        layout->setSpacing(10);
        auto *title = new QLabel(titleText, placeholder);
        title->setStyleSheet(QStringLiteral("color: #302A48; font-size: 23px; font-weight: 800;"));
        auto *description = new QLabel(descriptionText, placeholder);
        description->setWordWrap(true);
        description->setStyleSheet(QStringLiteral("color: #8F87A8; font-size: 13px;"));
        layout->addWidget(title);
        layout->addWidget(description);
        layout->addStretch(1);
        return placeholder;
    };
    m_workspaceStack->addWidget(makePlaceholderPage(
        QStringLiteral("收藏"), QStringLiteral("收藏功能暂未开放。当前阶段优先完善好友申请、好友列表和私聊入口。")));
    m_workspaceStack->addWidget(makePlaceholderPage(
        QStringLiteral("文件"), QStringLiteral("文件传输功能暂未开放；目前客户端支持文本消息和好友管理。")));

    connect(sideBar, &SideBar::sectionChanged, this,
            [this](int section)
            {
                if (m_workspaceStack == nullptr)
                {
                    return;
                }
                m_workspaceStack->setCurrentIndex(section);
                if (section == SideBar::Contacts)
                {
                    emit contactsRefreshRequested();
                }
            });

    rootLayout->addWidget(sideBar);
    rootLayout->addWidget(m_workspaceStack, 1);

    // 点击真实会话时同步标题、类型与数据库 conversation_id，并加载最新一页历史消息。
    connect(sessionPanel, &SessionListPanel::sessionActivated, this,
            [this, sessionPanel, chatArea](const QString &sessionId)
            {
                const SessionListModel *model = sessionPanel->sessionModel();
                for (int row = 0; row < model->rowCount(); ++row)
                {
                    const SessionItem *session = model->sessionAt(row);
                    if (session != nullptr && session->id == sessionId)
                    {
                        m_currentConversationId = session->conversationId;
                        m_currentConversationIsGroup = session->group;
                        chatArea->setConversationInfo(
                            session->name, session->avatarText,
                            session->group ? QStringLiteral("群聊") : QStringLiteral("私聊"),
                            session->group);
                        chatArea->setCurrentConversationId(m_currentConversationId);
                        if (m_currentConversationId > 0)
                        {
                            emit historyRequested(m_currentConversationId);
                        }
                        break;
                    }
                }
            });

    return page;
}

void MainWindow::setNetworkStatus(const QString &message, const QString &state)
{
    if (m_chatArea != nullptr)
    {
        m_chatArea->setNetworkStatus(message, state);
    }
}

void MainWindow::setMessageStatus(const QString &message, bool isError)
{
    if (m_chatArea != nullptr)
    {
        m_chatArea->setMessageStatus(message, isError);
    }
}


void MainWindow::setCurrentUsername(const QString &username)
{
    if (m_chatArea != nullptr)
    {
        m_chatArea->setCurrentUsername(username);
    }
}

bool MainWindow::appendMessage(const ChatMessageRecord &message)
{
    return m_chatArea != nullptr && m_chatArea->appendMessage(message);
}

int MainWindow::appendHistoryMessages(quint64 conversationId,
                                      const QVector<ChatMessageRecord> &messages)
{
    return m_chatArea != nullptr
        ? m_chatArea->appendHistoryMessages(conversationId, messages)
        : 0;
}

bool MainWindow::setMessageSendState(const QString &clientMessageId, MessageSendState state)
{
    return m_chatArea != nullptr &&
           m_chatArea->setMessageSendState(clientMessageId, state);
}

bool MainWindow::confirmPendingMessage(const QString &clientMessageId,
                                       quint64 serverMessageId,
                                       quint64 sequence)
{
    return m_chatArea != nullptr &&
           m_chatArea->confirmPendingMessage(clientMessageId, serverMessageId, sequence);
}

void MainWindow::clearMessages()
{
    if (m_chatArea != nullptr)
    {
        m_chatArea->clearMessages();
    }
}

void MainWindow::clearMessageInput()
{
    if (m_chatArea != nullptr)
    {
        m_chatArea->clearMessageInput();
    }
}

void MainWindow::setContacts(const QVector<ContactItem> &contacts)
{
    if (m_contactsPanel != nullptr)
    {
        m_contactsPanel->setFriends(contacts);
    }
}

void MainWindow::setPendingFriendRequests(const QVector<PendingFriendRequestItem> &requests)
{
    if (m_contactsPanel != nullptr)
    {
        m_contactsPanel->setPendingRequests(requests);
    }
}

void MainWindow::setContactsStatus(const QString &message, bool isError)
{
    if (m_contactsPanel != nullptr)
    {
        m_contactsPanel->setStatus(message, isError);
    }
}

void MainWindow::showMessagesPage()
{
    if (m_sideBar != nullptr)
    {
        m_sideBar->setCurrentSection(SideBar::Messages);
    }
    if (m_workspaceStack != nullptr)
    {
        m_workspaceStack->setCurrentIndex(SideBar::Messages);
    }
}

void MainWindow::finishAddFriendRequest(bool clearInput)
{
    if (m_contactsPanel != nullptr)
    {
        m_contactsPanel->finishAddFriendRequest(clearInput);
    }
}

// 为当前页面及所有子控件安装过滤器，这样鼠标停在聊天区/会话列表上时，
// 仍然可以从窗口最外侧的几个像素拖动边缘缩放，而不是只能拖动主窗口空白处。
void MainWindow::installResizeEventFilters(QWidget *widget)
{
    if (widget == nullptr)
    {
        return;
    }

    widget->installEventFilter(this);
    widget->setMouseTracking(true);
    const auto children = widget->findChildren<QWidget *>(QString(), Qt::FindDirectChildrenOnly);
    for (QWidget *child : children)
    {
        installResizeEventFilters(child);
    }
}

Qt::Edges MainWindow::resizeEdgesAt(const QPoint &globalPosition) const
{
    // 最大化窗口不允许边缘缩放，恢复普通窗口后即可再次调整大小。
    if (isMaximized() || isFullScreen())
    {
        return Qt::Edges();
    }

    constexpr int kResizeBorder = 7;
    const QRect bounds = frameGeometry();
    Qt::Edges edges;
    if (globalPosition.x() <= bounds.left() + kResizeBorder)
        edges |= Qt::LeftEdge;
    else if (globalPosition.x() >= bounds.right() - kResizeBorder)
        edges |= Qt::RightEdge;

    if (globalPosition.y() <= bounds.top() + kResizeBorder)
        edges |= Qt::TopEdge;
    else if (globalPosition.y() >= bounds.bottom() - kResizeBorder)
        edges |= Qt::BottomEdge;

    return edges;
}

void MainWindow::updateResizeCursor(QWidget *watched, const QPoint &globalPosition)
{
    if (watched == nullptr || m_resizing)
    {
        return;
    }

    const Qt::Edges edges = resizeEdgesAt(globalPosition);
    const Qt::CursorShape currentShape = watched->cursor().shape();
    const bool currentlyShowingResizeCursor =
        currentShape == Qt::SizeFDiagCursor || currentShape == Qt::SizeBDiagCursor ||
        currentShape == Qt::SizeHorCursor || currentShape == Qt::SizeVerCursor;

    // 不在边缘时，只清除本函数设置过的缩放光标，不覆盖文本框的 I-beam 等光标。
    if (edges == Qt::Edges())
    {
        if (currentlyShowingResizeCursor)
        {
            watched->unsetCursor();
        }
        return;
    }

    Qt::CursorShape shape = Qt::ArrowCursor;
    if ((edges.testFlag(Qt::LeftEdge) && edges.testFlag(Qt::TopEdge)) ||
        (edges.testFlag(Qt::RightEdge) && edges.testFlag(Qt::BottomEdge)))
    {
        shape = Qt::SizeFDiagCursor;
    }
    else if ((edges.testFlag(Qt::RightEdge) && edges.testFlag(Qt::TopEdge)) ||
             (edges.testFlag(Qt::LeftEdge) && edges.testFlag(Qt::BottomEdge)))
    {
        shape = Qt::SizeBDiagCursor;
    }
    else if (edges.testFlag(Qt::LeftEdge) || edges.testFlag(Qt::RightEdge))
    {
        shape = Qt::SizeHorCursor;
    }
    else if (edges.testFlag(Qt::TopEdge) || edges.testFlag(Qt::BottomEdge))
    {
        shape = Qt::SizeVerCursor;
    }

    watched->setCursor(shape);
}

// Qt 在窗口需要重绘时调用此函数，绘制设计稿中的紫灰背景光晕。
void MainWindow::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event);

    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);

    // 先填满整个客户区，保证最大化后四边和四角都覆盖屏幕。
    painter.fillRect(rect(), Theme::Page);

    QRadialGradient topGlow(QPointF(width() * 0.14, height() * 0.10),
                            qMax(width(), height()) * 0.48);
    topGlow.setColorAt(0.0, Theme::PurpleHalo);
    topGlow.setColorAt(0.55, QColor(174, 159, 255, 24));
    topGlow.setColorAt(1.0, QColor(174, 159, 255, 0));
    painter.fillRect(rect(), topGlow);

    QRadialGradient bottomGlow(QPointF(width() * 0.90, height() * 0.82),
                               qMax(width(), height()) * 0.42);
    bottomGlow.setColorAt(0.0, Theme::PurpleHaloSoft);
    bottomGlow.setColorAt(0.60, QColor(201, 190, 255, 13));
    bottomGlow.setColorAt(1.0, QColor(201, 190, 255, 0));
    painter.fillRect(rect(), bottomGlow);
}

// 窗口尺寸变化后，同步更新右上角控制按钮的位置。
void MainWindow::resizeEvent(QResizeEvent *event)
{
    QMainWindow::resizeEvent(event);
    if (m_windowControls != nullptr)
    {
        m_windowControls->move(width() - 18 - m_windowControls->width(), 14);
        m_windowControls->raise();
    }
}

// 直接在主窗口空白区域按下左键时，记录鼠标与窗口左上角的偏移量。
void MainWindow::mousePressEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton && !isMaximized() && !isFullScreen())
    {
        const QPoint globalPosition = event->globalPosition().toPoint();
        const Qt::Edges edges = resizeEdgesAt(globalPosition);
        if (edges != Qt::Edges())
        {
            m_resizing = true;
            m_resizeEdges = edges;
            m_resizeStartGlobal = globalPosition;
            m_resizeStartGeometry = geometry();
            event->accept();
            return;
        }

        m_dragging = true;
        m_dragOffset = globalPosition - frameGeometry().topLeft();
        event->accept();
        return;
    }
    QMainWindow::mousePressEvent(event);
}

// 左键按住移动鼠标时移动整个无边框窗口。
void MainWindow::mouseMoveEvent(QMouseEvent *event)
{
    const QPoint globalPosition = event->globalPosition().toPoint();
    if (m_resizing && (event->buttons() & Qt::LeftButton))
    {
        const QPoint delta = globalPosition - m_resizeStartGlobal;
        QRect nextGeometry = m_resizeStartGeometry;

        // 依据拖动方向调整窗口边缘，并始终遵守窗口最小宽高。
        if (m_resizeEdges.testFlag(Qt::LeftEdge))
            nextGeometry.setLeft(qMin(m_resizeStartGeometry.left() + delta.x(),
                                      m_resizeStartGeometry.right() - minimumWidth() + 1));
        if (m_resizeEdges.testFlag(Qt::RightEdge))
            nextGeometry.setRight(qMax(m_resizeStartGeometry.right() + delta.x(),
                                       m_resizeStartGeometry.left() + minimumWidth() - 1));
        if (m_resizeEdges.testFlag(Qt::TopEdge))
            nextGeometry.setTop(qMin(m_resizeStartGeometry.top() + delta.y(),
                                     m_resizeStartGeometry.bottom() - minimumHeight() + 1));
        if (m_resizeEdges.testFlag(Qt::BottomEdge))
            nextGeometry.setBottom(qMax(m_resizeStartGeometry.bottom() + delta.y(),
                                        m_resizeStartGeometry.top() + minimumHeight() - 1));

        setGeometry(nextGeometry);
        event->accept();
        return;
    }

    if (m_dragging && (event->buttons() & Qt::LeftButton))
    {
        move(globalPosition - m_dragOffset);
        event->accept();
        return;
    }
    QMainWindow::mouseMoveEvent(event);
}

// 左键松开后结束拖动状态。
void MainWindow::mouseReleaseEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton)
    {
        m_dragging = false;
        m_resizing = false;
        m_resizeEdges = Qt::Edges();
        event->accept();
        return;
    }
    QMainWindow::mouseReleaseEvent(event);
}

void MainWindow::leaveEvent(QEvent *event)
{
    unsetCursor();
    QMainWindow::leaveEvent(event);
}

// 让页面栈和页面空白处也能拖动窗口；控件内部的点击交互仍由控件自己处理。
bool MainWindow::eventFilter(QObject *watched, QEvent *event)
{
    auto *watchedWidget = qobject_cast<QWidget *>(watched);
    if (watchedWidget != nullptr)
    {
        if (event->type() == QEvent::MouseButtonPress)
        {
            auto *mouseEvent = static_cast<QMouseEvent *>(event);
            if (mouseEvent->button() == Qt::LeftButton)
            {
                const QPoint globalPosition = mouseEvent->globalPosition().toPoint();
                const Qt::Edges edges = resizeEdgesAt(globalPosition);
                if (edges != Qt::Edges())
                {
                    // 先处理边缘缩放，避免它被页面拖动逻辑当成移动窗口。
                    m_resizing = true;
                    m_dragging = false;
                    m_resizeEdges = edges;
                    m_resizeStartGlobal = globalPosition;
                    m_resizeStartGeometry = geometry();
                    // 事件被过滤器拦截后，主动抓取鼠标，确保拖出当前子控件时仍能持续缩放。
                    watchedWidget->grabMouse();
                    return true;
                }

                // 只有页面背景和页面栈本身负责拖动窗口；普通控件保留自己的交互。
                if (!isMaximized() && !isFullScreen() &&
                    (watched == m_pageStack || watched == m_pageStack->currentWidget()))
                {
                    m_dragging = true;
                    m_dragOffset = globalPosition - frameGeometry().topLeft();
                    return true;
                }
            }
        }
        else if (event->type() == QEvent::MouseMove)
        {
            auto *mouseEvent = static_cast<QMouseEvent *>(event);
            const QPoint globalPosition = mouseEvent->globalPosition().toPoint();

            if (m_resizing && (mouseEvent->buttons() & Qt::LeftButton))
            {
                const QPoint delta = globalPosition - m_resizeStartGlobal;
                QRect nextGeometry = m_resizeStartGeometry;
                if (m_resizeEdges.testFlag(Qt::LeftEdge))
                    nextGeometry.setLeft(qMin(m_resizeStartGeometry.left() + delta.x(),
                                              m_resizeStartGeometry.right() - minimumWidth() + 1));
                if (m_resizeEdges.testFlag(Qt::RightEdge))
                    nextGeometry.setRight(qMax(m_resizeStartGeometry.right() + delta.x(),
                                               m_resizeStartGeometry.left() + minimumWidth() - 1));
                if (m_resizeEdges.testFlag(Qt::TopEdge))
                    nextGeometry.setTop(qMin(m_resizeStartGeometry.top() + delta.y(),
                                             m_resizeStartGeometry.bottom() - minimumHeight() + 1));
                if (m_resizeEdges.testFlag(Qt::BottomEdge))
                    nextGeometry.setBottom(qMax(m_resizeStartGeometry.bottom() + delta.y(),
                                                m_resizeStartGeometry.top() + minimumHeight() - 1));
                setGeometry(nextGeometry);
                return true;
            }

            if (m_dragging && (mouseEvent->buttons() & Qt::LeftButton) &&
                (watched == m_pageStack || watched == m_pageStack->currentWidget()))
            {
                move(globalPosition - m_dragOffset);
                return true;
            }

            updateResizeCursor(watchedWidget, globalPosition);
        }
        else if (event->type() == QEvent::MouseButtonRelease)
        {
            auto *mouseEvent = static_cast<QMouseEvent *>(event);
            if (mouseEvent->button() == Qt::LeftButton)
            {
                if (m_resizing)
                {
                    m_resizing = false;
                    m_resizeEdges = Qt::Edges();
                    watchedWidget->releaseMouse();
                    return true;
                }
                if (m_dragging && (watched == m_pageStack || watched == m_pageStack->currentWidget()))
                {
                    m_dragging = false;
                    return true;
                }
            }
        }
        else if (event->type() == QEvent::Leave)
        {
            const Qt::CursorShape shape = watchedWidget->cursor().shape();
            if (shape == Qt::SizeFDiagCursor || shape == Qt::SizeBDiagCursor ||
                shape == Qt::SizeHorCursor || shape == Qt::SizeVerCursor)
            {
                watchedWidget->unsetCursor();
            }
        }
    }

    return QMainWindow::eventFilter(watched, event);
}


void MainWindow::setHistoryLoading(quint64 conversationId, bool loading, bool hasMore)
{
    if (conversationId == m_currentConversationId && m_chatArea != nullptr)
    {
        m_chatArea->setHistoryLoading(loading, hasMore);
    }
}

void MainWindow::setSessions(const QVector<SessionItem> &sessions,
                             quint64 preferredConversationId)
{
    if (m_sessionPanel == nullptr)
    {
        return;
    }

    m_sessionPanel->setSessions(sessions, preferredConversationId);
    if (sessions.isEmpty())
    {
        m_currentConversationId = 0;
        m_currentConversationIsGroup = false;
        if (m_chatArea != nullptr)
        {
            m_chatArea->setCurrentConversationId(0);
            m_chatArea->setConversationInfo(
                QStringLiteral("暂无会话"), QString(), QStringLiteral("点击 + 选择好友开始私聊"));
            m_chatArea->setHistoryLoading(false, true);
        }
    }
}

void MainWindow::clearSessions()
{
    setSessions({});
}
