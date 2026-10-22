#pragma once

#include <QMainWindow>
#include <QPoint>

class QEvent;
class QMouseEvent;
class QPaintEvent;
class QStackedWidget;
class QWidget;

// 客户端主窗口。
// 当前阶段只搭建窗口骨架和页面切换容器；欢迎页、登录页、聊天页会在后续阶段逐步完善。
class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    // parent 默认为空，表示主窗口没有其他 Qt 父窗口。
    explicit MainWindow(QWidget *parent = nullptr);

protected:
    // 绘制窗口底色与紫色光晕，模拟最终 HTML 设计稿中的页面背景。
    void paintEvent(QPaintEvent *event) override;
    // 以下三个鼠标事件用于拖动无系统标题栏的窗口。
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    // 页面控件会先收到鼠标事件；事件过滤器让页面空白处也能拖动窗口。
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    // 创建统一样式的占位页面，避免三个页面在基础阶段重复编写布局代码。
    QWidget *createPlaceholderPage(const QString &title,
                                   const QString &description,
                                   QWidget *parent = nullptr);

    QStackedWidget *m_pages = nullptr; // 管理欢迎、登录和聊天三个页面，只显示当前页。
    QPoint m_dragOffset;              // 鼠标按下位置相对窗口左上角的偏移量。
    bool m_dragging = false;          // 标记当前是否处于按住鼠标左键拖动窗口的状态。
};
