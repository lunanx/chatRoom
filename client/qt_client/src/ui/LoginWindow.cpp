#include "LoginWindow.h"

#include "ui/WelcomePage.h"
#include "ui/AuthPage.h"

#include <QStackedWidget>
#include <QWidget>

LoginWindow::LoginWindow(QWidget *parent)
    : QMainWindow(parent),
      m_pageStack(new QStackedWidget(this))
{
    setWindowTitle(QStringLiteral("Go聊 - 登录"));
    setFixedSize(400, 550);

    // 创建欢迎页和登录/注册页。
    m_welcomePage = new WelcomePage(m_pageStack);
    m_authPage = new AuthPage(m_pageStack);

    m_pageStack->addWidget(m_welcomePage);
    m_pageStack->addWidget(m_authPage);

    // 登录窗口启动时首先显示欢迎页。
    m_pageStack->setCurrentWidget(m_welcomePage);
    setCentralWidget(m_pageStack);

    // 欢迎页点击“进入”后，切换到登录/注册页。
    connect(m_welcomePage, &WelcomePage::enterRequested,
            this, [this]()
    {
        m_pageStack->setCurrentWidget(m_authPage);
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