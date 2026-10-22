#pragma once

#include <QMainWindow>
#include <QPoint>
#include <QRect>
#include <Qt>
#include <QString>

class QEvent;
class QMouseEvent;
class QPaintEvent;
class QResizeEvent;
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
    void leaveEvent(QEvent *event) override;
    // 页面空白区域的鼠标事件也交由主窗口处理，同时监控无边框窗口的缩放边缘。
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    // 将 SideBar、SessionListPanel、ChatArea 组合为完整聊天工作台。
    QWidget *createChatWorkspacePage();
    // 给页面及其子控件安装事件过滤器，使鼠标拖到窗口边缘时也能缩放。
    void installResizeEventFilters(QWidget *widget);
    // 根据全局鼠标位置判断当前位于哪个缩放边缘。
    Qt::Edges resizeEdgesAt(const QPoint &globalPosition) const;
    // 根据边缘方向设置合适的鼠标指针。
    void updateResizeCursor(QWidget *watched, const QPoint &globalPosition);

    QStackedWidget *m_pageStack = nullptr; // 页面栈，只显示当前页面。
    QPoint m_dragOffset;              // 鼠标按下位置相对窗口左上角的偏移量。
    bool m_dragging = false;          // 当前是否正在拖动窗口。
    bool m_resizing = false;          // 当前是否正在拖动窗口边缘缩放。
    Qt::Edges m_resizeEdges;          // 当前正在拖动的边缘/角。
    QPoint m_resizeStartGlobal;       // 缩放开始时的全局鼠标坐标。
    QRect m_resizeStartGeometry;      // 缩放开始时的窗口几何区域。
};
