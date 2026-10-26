#pragma once

#include <QWidget>
#include <QString>

class QCheckBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QEvent;
class QResizeEvent;
class QWidget;

// Go聊登录/注册页：左侧品牌介绍，右侧账号表单。
class AuthPage : public QWidget
{
    Q_OBJECT

public:
    explicit AuthPage(QWidget *parent = nullptr);

    // 显示当前 TCP 连接状态，不代替服务端的登录结果。
    void setNetworkStatus(const QString &message, const QString &state);

    // 便于外部将光标放入账号输入框。
    void focusAccountInput();

signals:
    // 页面只传递表单内容，不在页面内部执行网络登录。
    void loginRequested(const QString &account,
                        const QString &password,
                        bool rememberMe);
    void registerRequested(const QString &account,
                           const QString &password);

protected:
    // 监听输入框焦点变化，为聚焦状态启用紫色柔光。
    bool eventFilter(QObject *watched, QEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;

private:
    QLabel *m_networkStatusLabel = nullptr;
    QLineEdit *m_accountEdit = nullptr;
    QLineEdit *m_passwordEdit = nullptr;
    QCheckBox *m_rememberCheckBox = nullptr;
    QPushButton *m_loginButton = nullptr;
    QPushButton *m_registerButton = nullptr;
    QWidget *m_windowControls = nullptr;
};
