#include "MainWindow.h"
#include "WindowTopControls.h"

#include "Theme.h"
#include "ui/ChatArea.h"
#include "ui/SessionListPanel.h"
#include "ui/SideBar.h"

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

// 初始化无边框窗口和三个页面：欢迎页、登录页、聊天工作台。
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

// 将三个已实现的组件组合成工作台：导航栏 | 会话列表 | 当前聊天区域。
QWidget *MainWindow::createChatWorkspacePage()
{
    auto *page = new QWidget;
    page->setObjectName(QStringLiteral("chatWorkspacePage"));
    page->setAttribute(Qt::WA_TranslucentBackground);
    page->setStyleSheet(QStringLiteral("QWidget#chatWorkspacePage { background: transparent; }"));

    // QSplitter 允许用户拖动会话列表与聊天区之间的分隔条。
    auto *splitter = new QSplitter(Qt::Horizontal, page);
    splitter->setObjectName(QStringLiteral("chatWorkspaceSplitter"));
    splitter->setChildrenCollapsible(false);
    // 三栏之间只保留 0px 的细间距，QSplitter 仍然可以拖动调整宽度。
    splitter->setHandleWidth(0);
    splitter->setOpaqueResize(true);

    auto *sideBar = new SideBar(splitter);
    sideBar->setFixedWidth(Theme::NavigationWidth);

    // 主窗口不直接处理退出登录，只把事件转发给 AppController。
    connect(sideBar, &SideBar::logoutRequested,
            this, &MainWindow::logoutRequested);

    auto *sessionPanel = new SessionListPanel(splitter);
    // SessionListPanel 自己设置了 220–340px 的宽度范围；这里给它 240px 初始宽度。
    sessionPanel->resize(Theme::ConversationWidth, sessionPanel->height());

    m_chatArea = new ChatArea(splitter);
    ChatArea *chatArea = m_chatArea;
    chatArea->setConversationInfo(QStringLiteral("林间风"), QStringLiteral("林"), true);

    // 输入区只发出“用户想发送什么”；网络请求由 AppController 统一处理。
    connect(chatArea, &ChatArea::sendRequested, this,
            [this](const QString &text)
            {
                emit messageSendRequested(
                    text, m_currentConversationId, m_currentConversationIsGroup);
            });

    splitter->addWidget(sideBar);
    splitter->addWidget(sessionPanel);
    splitter->addWidget(chatArea);
    splitter->setStretchFactor(0, 0); // 导航栏固定宽度。
    splitter->setStretchFactor(1, 0); // 会话列表可拖动，但不主动抢占剩余空间。
    splitter->setStretchFactor(2, 1); // 窗口剩余宽度优先交给聊天区域。
    splitter->setSizes({Theme::NavigationWidth, Theme::ConversationWidth, 900});

    // 用外边距和间隔把三个区域分开，保留紫灰主题的呼吸感。
    auto *layout = new QHBoxLayout(page);
    // 整个窗口不留外框空隙，三栏工作台直接铺满客户区。
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    layout->addWidget(splitter);

    // 点击会话时，把会话模型里的名字和头像文字同步到聊天区 Header。
    // 这里仅联动演示数据，不代表已经连接服务器或加载了真实消息。
    connect(sessionPanel, &SessionListPanel::sessionActivated, this,
            [this, sessionPanel, chatArea](const QString &sessionId)
            {
                const SessionListModel *model = sessionPanel->sessionModel();
                for (int row = 0; row < model->rowCount(); ++row)
                {
                    const SessionItem *session = model->sessionAt(row);
                    if (session != nullptr && session->id == sessionId)
                    {
                        chatArea->setConversationInfo(session->name, session->avatarText, true);
                        m_currentConversationId = session->conversationId;
                        m_currentConversationIsGroup = session->group;
                        break;
                    }
                }
            });

    // 如果列表已有会话并默认选中了第一条，初始化其 ID/type；后续真实会话数据接入后无需额外点击。
    if (const SessionItem *firstSession = sessionPanel->sessionModel()->sessionAt(0))
    {
        m_currentConversationId = firstSession->conversationId;
        m_currentConversationIsGroup = firstSession->group;
        chatArea->setConversationInfo(
            firstSession->name, firstSession->avatarText, true);
    }

    return page;
}

void MainWindow::setMessageStatus(const QString &message, bool isError)
{
    if (m_chatArea != nullptr)
    {
        m_chatArea->setMessageStatus(message, isError);
    }
}

void MainWindow::clearMessageInput()
{
    if (m_chatArea != nullptr)
    {
        m_chatArea->clearMessageInput();
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
