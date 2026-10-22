#pragma once

#include <QMainWindow>
#include <QPoint>
#include <QString>

class QEvent;
class QMouseEvent;
class QPaintEvent;
class QStackedWidget;
class QWidget;

// 客户端主窗口：管理欢迎页、登录页和聊天工作台三个页面。
class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);

protected:
    // 绘制窗口底色与紫色光晕，模拟最终 HTML 设计稿中的页面背景。
    void paintEvent(QPaintEvent *event) override;
    // 无系统标题栏时，允许用户拖动窗口。
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    // 页面空白区域的鼠标事件也交由主窗口处理。
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    // 创建暂时还未实现的欢迎页和登录页。
    QWidget *createPlaceholderPage(const QString &title,
                                   const QString &description,
                                   QWidget *parent = nullptr);
    // 将 SideBar、SessionListPanel、ChatArea 组合为完整聊天工作台。
    QWidget *createChatWorkspacePage();

    QStackedWidget *m_pages = nullptr; // 页面栈，只显示当前页面。
    QPoint m_dragOffset;              // 鼠标按下位置相对窗口左上角的偏移量。
    bool m_dragging = false;          // 当前是否正在拖动窗口。
};
