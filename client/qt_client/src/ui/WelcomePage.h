#pragma once

#include <QWidget>

class QPushButton;

// Go聊欢迎页：展示品牌标识和进入按钮。
class WelcomePage : public QWidget
{
    Q_OBJECT

public:
    explicit WelcomePage(QWidget *parent = nullptr);

signals:
    // 用户点击“进入”时发出，由 MainWindow 切换到登录页。
    void enterRequested();

protected:
    // 绘制背景上的柔和紫色光晕。
    void paintEvent(QPaintEvent *event) override;

private:
    QPushButton *m_enterButton = nullptr;
};
