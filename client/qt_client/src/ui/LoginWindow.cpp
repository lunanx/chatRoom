#include "LoginWindow.h"

#include "ui/WelcomePage.h"
#include "ui/AuthPage.h"

#include <QEvent>
#include <QFrame>
#include <QMouseEvent>
#include <QStackedWidget>
#include <QWidget>

LoginWindow::LoginWindow(QWidget *parent)
    : QMainWindow(parent),
      m_pageStack(new QStackedWidget(this))
{
    setWindowTitle(QStringLiteral("Go聊"));
    // 去掉系统标题栏，窗口按钮由欢迎页/登录注册页自己显示。
    setWindowFlags(Qt::Window | Qt::FramelessWindowHint);
    // 窗口使用普通不透明客户区，四角为直角；不设置透明窗口遮罩，避免最大化时露出屏幕边缘。
    // 欢迎页采用紧凑尺寸；进入登录/注册页后再调整到表单所需大小。
    setMinimumSize(0, 0);
    resize(400, 500);

    // 页面栈铺满窗口客户区，不添加额外边框。
    m_pageStack->setFrameShape(QFrame::NoFrame);
    m_pageStack->setStyleSheet(QStringLiteral(
        "QStackedWidget { background: transparent; border: none; }"));

    // 创建欢迎页和登录/注册页。
    m_welcomePage = new WelcomePage(m_pageStack);
    m_authPage = new AuthPage(m_pageStack);

    m_pageStack->addWidget(m_welcomePage);
    m_pageStack->addWidget(m_authPage);

    // 在页面背景上按住鼠标时可以拖动无边框窗口。
    m_pageStack->installEventFilter(this);
    m_welcomePage->installEventFilter(this);
    m_authPage->installEventFilter(this);

    // 登录窗口启动时首先显示欢迎页。
    m_pageStack->setCurrentWidget(m_welcomePage);
    setCentralWidget(m_pageStack);

    // 欢迎页点击“进入”后，切换到登录/注册页。
    connect(m_welcomePage, &WelcomePage::enterRequested,
            this, [this]()
    {
        m_pageStack->setCurrentWidget(m_authPage);
        // 登录/注册页包含左侧介绍区和右侧表单，使用刚好容纳内容的尺寸。
        resize(820, 600);
        m_authPage->focusAccountInput();
    });

    // 将 AuthPage 的登录请求转发给 LoginWindow 的外部使用者。
    connect(m_authPage, &AuthPage::loginRequested,
            this, &LoginWindow::loginRequested);

    // 将注册请求同样转发出去。
    connect(m_authPage, &AuthPage::registerRequested,
            this, &LoginWindow::registerRequested);
}

// 由登录业务在确认验证成功后调用。
// 这里不直接判断账号密码，也不假装服务器已经验证成功。
void LoginWindow::notifyLoginSucceeded()
{
    emit loginSucceeded();
}

// 无系统标题栏后，允许从页面背景区域拖动窗口。
bool LoginWindow::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == m_pageStack || watched == m_welcomePage || watched == m_authPage)
    {
        if (event->type() == QEvent::MouseButtonPress)
        {
            auto *mouseEvent = static_cast<QMouseEvent *>(event);
            if (mouseEvent->button() == Qt::LeftButton && !isMaximized() && !isFullScreen())
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
            }
        }
    }

    return QMainWindow::eventFilter(watched, event);
}
