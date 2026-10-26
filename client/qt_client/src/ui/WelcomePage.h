#pragma once

#include <QWidget>
#include <QString>

class QLabel;
class QPushButton;
class QWidget;
class QResizeEvent;

// Go聊欢迎页：展示品牌标识和进入按钮。
class WelcomePage : public QWidget
{
    Q_OBJECT

public:
    explicit WelcomePage(QWidget *parent = nullptr);

    // 在欢迎页底部显示轻量的服务器连接状态。
    void setNetworkStatus(const QString &message, const QString &state);

signals:
    // 用户点击“进入”时发出，由 MainWindow 切换到登录页。
    void enterRequested();

protected:
    // 绘制背景上的柔和紫色光晕。
    void paintEvent(QPaintEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;

private:
    QPushButton *m_enterButton = nullptr;
    QLabel *m_footnote = nullptr;
    QWidget *m_windowControls = nullptr;
};
