#include "MainWindow.h"

#include "Theme.h"

#include <QEvent>
#include <QFont>
#include <QFrame>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QRadialGradient>
#include <QStackedWidget>
#include <QVBoxLayout>
#include <QWidget>

// 初始化窗口属性、页面容器，并先放入三个可运行的占位页面。
MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent),
      m_pages(new QStackedWidget(this))
{
    // 隐藏操作系统自带标题栏，后续由我们自己绘制窗口控制按钮。
    setWindowTitle(QStringLiteral("Go聊"));
    setWindowFlags(Qt::Window | Qt::FramelessWindowHint);
    setAttribute(Qt::WA_TranslucentBackground, false);
    setMinimumSize(Theme::WindowMinimumWidth, Theme::WindowMinimumHeight);
    resize(1280, 800);

    // 窗口与中央页面保持透明，让 MainWindow::paintEvent 绘制紫灰光晕背景。
    m_pages->setObjectName(QStringLiteral("pageStack"));
    m_pages->installEventFilter(this);
    m_pages->setFrameShape(QFrame::NoFrame);
    m_pages->setAttribute(Qt::WA_TranslucentBackground);
    m_pages->setStyleSheet(QStringLiteral("QStackedWidget#pageStack { background: transparent; border: none; }"));
    setCentralWidget(m_pages);

    // 基础阶段先创建三个独立占位页，后续逐页替换成真实 UI。
    QWidget *welcomePage = createPlaceholderPage(
        QStringLiteral("Go聊"),
        QStringLiteral("说 Go 就 Go · 欢迎页占位"));
    QWidget *authPage = createPlaceholderPage(
        QStringLiteral("登录 / 注册"),
        QStringLiteral("账号与密码表单将在下一阶段实现"));
    QWidget *chatPage = createPlaceholderPage(
        QStringLiteral("聊天工作台"),
        QStringLiteral("导航栏、会话列表与聊天区域将在下一阶段实现"));

    // 页面空白区域也支持拖动无边框窗口。后续加入按钮、输入框后，
    // 交互控件本身不应安装这个事件过滤器，避免拖动与点击冲突。
    welcomePage->installEventFilter(this);
    authPage->installEventFilter(this);
    chatPage->installEventFilter(this);

    m_pages->addWidget(welcomePage);
    m_pages->addWidget(authPage);
    m_pages->addWidget(chatPage);
    m_pages->setCurrentWidget(welcomePage);
}

// 创建一个垂直居中的简单页面：品牌标记、标题和说明文字。
// 这里先用占位内容验证窗口骨架，后续会替换为最终设计稿中的真实控件。
QWidget *MainWindow::createPlaceholderPage(const QString &title,
                                           const QString &description,
                                           QWidget *parent)
{
    // page 是本页面所有控件的父对象；页面销毁时，子控件会由 Qt 自动释放。
    auto *page = new QWidget(parent);
    page->setAttribute(Qt::WA_TranslucentBackground);
    page->setStyleSheet(QStringLiteral("background: transparent;"));

    // QVBoxLayout 按垂直方向排列子控件；上下 stretch 将内容推到页面中间。
    auto *layout = new QVBoxLayout(page);
    layout->setContentsMargins(48, 48, 48, 48);
    layout->setSpacing(14);
    layout->addStretch(1);

    // 临时品牌标记使用 CSS 渐变，颜色与 HTML 设计稿的品牌渐变保持一致。
    auto *brand = new QLabel(QStringLiteral("G"), page);
    brand->setAlignment(Qt::AlignCenter);
    brand->setFixedSize(72, 72);
    brand->setStyleSheet(QStringLiteral(
        "QLabel { color: white; background: qlineargradient(x1:0,y1:0,x2:1,y2:1,"
        "stop:0 #806BFF, stop:0.52 #6A53F6, stop:1 #927BFF);"
        "border-radius: 25px; font-size: 38px; font-weight: 900; }"));

    auto *brandRow = new QWidget(page);
    auto *brandLayout = new QVBoxLayout(brandRow);
    brandLayout->setContentsMargins(0, 0, 0, 0);
    brandLayout->addWidget(brand, 0, Qt::AlignHCenter);

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
    descriptionLabel->setStyleSheet(QStringLiteral("color: #77708F; font-size: 13px; background: transparent;"));

    layout->addWidget(brandRow);
    layout->addWidget(titleLabel);
    layout->addWidget(descriptionLabel);
    layout->addStretch(1);

    return page;
}

// Qt 在窗口需要重绘时调用此函数。这里直接绘制背景，不创建额外背景控件。
void MainWindow::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event);

    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);

    // 用两个径向渐变模拟 HTML 的 --canvas-gradient。
    painter.fillRect(rect(), Theme::Page);

    // 左上方的柔和紫色光晕：中心颜色较明显，向外逐渐透明。
    QRadialGradient topGlow(QPointF(width() * 0.14, height() * 0.10),
                            qMax(width(), height()) * 0.48);
    topGlow.setColorAt(0.0, Theme::PurpleHalo);
    topGlow.setColorAt(0.55, QColor(174, 159, 255, 24));
    topGlow.setColorAt(1.0, QColor(174, 159, 255, 0));
    painter.fillRect(rect(), topGlow);

    // 右下方叠加第二层淡光晕，形成设计稿中的轻微层次感。
    QRadialGradient bottomGlow(QPointF(width() * 0.90, height() * 0.82),
                               qMax(width(), height()) * 0.42);
    bottomGlow.setColorAt(0.0, Theme::PurpleHaloSoft);
    bottomGlow.setColorAt(0.60, QColor(201, 190, 255, 13));
    bottomGlow.setColorAt(1.0, QColor(201, 190, 255, 0));
    painter.fillRect(rect(), bottomGlow);
}

// 鼠标左键按下时记录鼠标与窗口左上角的距离，供后续移动计算使用。
void MainWindow::mousePressEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton)
    {
        m_dragging = true;
        // 使用屏幕全局坐标计算偏移量，窗口移动时鼠标不会突然跳到窗口角落。
        m_dragOffset = event->globalPosition().toPoint() - frameGeometry().topLeft();
        event->accept();
        return;
    }

    QMainWindow::mousePressEvent(event);
}

// 只有左键仍按住时才移动窗口；普通鼠标移动不会改变窗口位置。
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

// 左键松开后结束拖动状态，避免后续鼠标移动继续带动窗口。
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


// 页面上的鼠标事件通常先发给页面，而不是主窗口，因此在这里补充拖动处理。
// 当前仅给页面容器和占位页面安装过滤器；添加按钮、输入框后，不要把过滤器
// 安装到这些交互控件上，否则按下控件时可能被误认为是在拖动窗口。
bool MainWindow::eventFilter(QObject *watched, QEvent *event)
{
    // 只处理页面容器或当前显示的页面，其他对象的事件交回 Qt 默认处理。
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
