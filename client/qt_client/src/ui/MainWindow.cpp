#include "MainWindow.h"

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
#include <QPainter>
#include <QPainterPath>
#include <QRadialGradient>
#include <QSplitter>
#include <QStackedWidget>
#include <QVBoxLayout>
#include <QWidget>

// 初始化无边框窗口和三个页面：欢迎页、登录页、聊天工作台。
MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent),
      m_pages(new QStackedWidget(this))
{
    setWindowTitle(QStringLiteral("Go聊"));
    setWindowFlags(Qt::Window | Qt::FramelessWindowHint);
    setAttribute(Qt::WA_TranslucentBackground, false);
    setMinimumSize(Theme::WindowMinimumWidth, Theme::WindowMinimumHeight);
    resize(1280, 800);

    // 页面栈背景保持透明，让 paintEvent 统一绘制窗口的紫灰渐变光晕。
    m_pages->setObjectName(QStringLiteral("pageStack"));
    m_pages->installEventFilter(this);
    m_pages->setFrameShape(QFrame::NoFrame);
    m_pages->setAttribute(Qt::WA_TranslucentBackground);
    m_pages->setStyleSheet(QStringLiteral(
        "QStackedWidget#pageStack { background: transparent; border: none; }"));
    setCentralWidget(m_pages);

    QWidget *welcomePage = createPlaceholderPage(
        QStringLiteral("Go聊"),
        QStringLiteral("说 Go 就 Go · 欢迎页占位"));
    QWidget *authPage = createPlaceholderPage(
        QStringLiteral("登录 / 注册"),
        QStringLiteral("账号与密码表单将在后续阶段实现"));
    QWidget *chatPage = createChatWorkspacePage();

    // 页面空白边缘区域支持拖动无边框窗口。交互控件本身不安装此过滤器，
    // 避免按钮点击、输入文字或拖动分隔条时误触发窗口拖动。
    welcomePage->installEventFilter(this);
    authPage->installEventFilter(this);
    chatPage->installEventFilter(this);

    m_pages->addWidget(welcomePage);
    m_pages->addWidget(authPage);
    m_pages->addWidget(chatPage);

    // 当前先直接进入聊天工作台，方便集中检查三栏组合效果。
    // 欢迎页和登录页仍保留在页面栈中，后续可增加页面切换逻辑。
    m_pages->setCurrentWidget(chatPage);
}

// 创建一个垂直居中的简单占位页面，供欢迎页和登录页暂时使用。
QWidget *MainWindow::createPlaceholderPage(const QString &title,
                                           const QString &description,
                                           QWidget *parent)
{
    auto *page = new QWidget(parent);
    page->setAttribute(Qt::WA_TranslucentBackground);
    page->setStyleSheet(QStringLiteral("background: transparent;"));

    auto *layout = new QVBoxLayout(page);
    layout->setContentsMargins(48, 48, 48, 48);
    layout->setSpacing(14);
    layout->addStretch(1);

    auto *brand = new QLabel(QStringLiteral("G"), page);
    brand->setAlignment(Qt::AlignCenter);
    brand->setFixedSize(72, 72);
    brand->setStyleSheet(QStringLiteral(
        "QLabel { color: white; background: qlineargradient(x1:0,y1:0,x2:1,y2:1,"
        "stop:0 #806BFF, stop:0.52 #6A53F6, stop:1 #927BFF);"
        "border-radius: 25px; font-size: 38px; font-weight: 900; }"));

    auto *titleLabel = new QLabel(title, page);
    titleLabel->setAlignment(Qt::AlignCenter);
    QFont titleFont = titleLabel->font();
    titleFont.setFamily(QStringLiteral("Microsoft YaHei UI"));
    titleFont.setPointSize(25);
    titleFont.setBold(true);
    titleLabel->setFont(titleFont);
    titleLabel->setStyleSheet(QStringLiteral("color: #302A48; background: transparent;"));

    auto *descriptionLabel = new QLabel(description, page);
    descriptionLabel->setAlignment(Qt::AlignCenter);
    descriptionLabel->setWordWrap(true);
    descriptionLabel->setStyleSheet(QStringLiteral(
        "color: #77708F; font-size: 13px; background: transparent;"));

    layout->addWidget(brand, 0, Qt::AlignHCenter);
    layout->addWidget(titleLabel);
    layout->addWidget(descriptionLabel);
    layout->addStretch(1);
    return page;
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
    splitter->setHandleWidth(8);
    splitter->setOpaqueResize(true);

    auto *sideBar = new SideBar(splitter);
    sideBar->setFixedWidth(Theme::NavigationWidth);

    auto *sessionPanel = new SessionListPanel(splitter);
    // SessionListPanel 自己设置了 220–340px 的宽度范围；这里给它 240px 初始宽度。
    sessionPanel->resize(Theme::ConversationWidth, sessionPanel->height());

    auto *chatArea = new ChatArea(splitter);
    chatArea->setConversationInfo(QStringLiteral("林间风"), QStringLiteral("林"), true);

    splitter->addWidget(sideBar);
    splitter->addWidget(sessionPanel);
    splitter->addWidget(chatArea);
    splitter->setStretchFactor(0, 0); // 导航栏固定宽度。
    splitter->setStretchFactor(1, 0); // 会话列表可拖动，但不主动抢占剩余空间。
    splitter->setStretchFactor(2, 1); // 窗口剩余宽度优先交给聊天区域。
    splitter->setSizes({Theme::NavigationWidth, Theme::ConversationWidth, 900});

    // 用外边距和间隔把三个区域分开，保留紫灰主题的呼吸感。
    auto *layout = new QHBoxLayout(page);
    layout->setContentsMargins(16, 16, 16, 16);
    layout->setSpacing(0);
    layout->addWidget(splitter);

    // 点击会话时，把会话模型里的名字和头像文字同步到聊天区 Header。
    // 这里仅联动演示数据，不代表已经连接服务器或加载了真实消息。
    connect(sessionPanel, &SessionListPanel::sessionActivated, this,
            [sessionPanel, chatArea](const QString &sessionId)
    {
        const SessionListModel *model = sessionPanel->sessionModel();
        for (int row = 0; row < model->rowCount(); ++row)
        {
            const SessionItem *session = model->sessionAt(row);
            if (session != nullptr && session->id == sessionId)
            {
                chatArea->setConversationInfo(session->name, session->avatarText, true);
                break;
            }
        }
    });

    return page;
}

// Qt 在窗口需要重绘时调用此函数，绘制设计稿中的紫灰背景光晕。
void MainWindow::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event);

    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);
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

// 直接在主窗口空白区域按下左键时，记录鼠标与窗口左上角的偏移量。
void MainWindow::mousePressEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton)
    {
        m_dragging = true;
        m_dragOffset = event->globalPosition().toPoint() - frameGeometry().topLeft();
        event->accept();
        return;
    }
    QMainWindow::mousePressEvent(event);
}

// 左键按住移动鼠标时移动整个无边框窗口。
void MainWindow::mouseMoveEvent(QMouseEvent *event)
{
    if (m_dragging && (event->buttons() & Qt::LeftButton))
    {
        move(event->globalPosition().toPoint() - m_dragOffset);
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
        event->accept();
        return;
    }
    QMainWindow::mouseReleaseEvent(event);
}

// 让页面栈和页面空白处也能拖动窗口；控件内部的点击交互仍由控件自己处理。
bool MainWindow::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == m_pages || watched == m_pages->currentWidget())
    {
        if (event->type() == QEvent::MouseButtonPress)
        {
            auto *mouseEvent = static_cast<QMouseEvent *>(event);
            if (mouseEvent->button() == Qt::LeftButton)
            {
                m_dragging = true;
                m_dragOffset = mouseEvent->globalPosition().toPoint() - frameGeometry().topLeft();
                return true;
            }
        }
        else if (event->type() == QEvent::MouseMove && m_dragging)
        {
            auto *mouseEvent = static_cast<QMouseEvent *>(event);
            if (mouseEvent->buttons() & Qt::LeftButton)
            {
                move(mouseEvent->globalPosition().toPoint() - m_dragOffset);
                return true;
            }
        }
        else if (event->type() == QEvent::MouseButtonRelease)
        {
            auto *mouseEvent = static_cast<QMouseEvent *>(event);
            if (mouseEvent->button() == Qt::LeftButton)
            {
                m_dragging = false;
                return true;
            }
        }
    }
    return QMainWindow::eventFilter(watched, event);
}
