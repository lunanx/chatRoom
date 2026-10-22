#pragma once

#include <QMainWindow>
#include <QPoint>

class QEvent;
class QMouseEvent;
class QStackedWidget;
class WelcomePage;
class AuthPage;

// 登录窗口：只负责欢迎页、登录页和注册页的切换。
class LoginWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit LoginWindow(QWidget *parent = nullptr);

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

public slots:
    // 只有在登录业务确认成功后，外部才能调用这个函数。
    void notifyLoginSucceeded();

signals:
    // 向外转发登录和注册请求，方便后续接入 Controller。
    void loginRequested(const QString &account,
                        const QString &password,
                        bool rememberMe);

    void registerRequested(const QString &account,
                           const QString &password);

    // 通知程序入口：登录已经成功。
    void loginSucceeded();

private:
    QStackedWidget *m_pageStack = nullptr;
    WelcomePage *m_welcomePage = nullptr;
    AuthPage *m_authPage = nullptr;
    bool m_dragging = false;
    QPoint m_dragOffset;
};