#include "WelcomePage.h"

#include <QFile>
#include <QLabel>
#include <QPaintEvent>
#include <QPainter>
#include <QPushButton>
#include <QHBoxLayout>
#include <QVBoxLayout>

namespace
{
    // 从 Qt 资源系统加载当前页面的 QSS。
    void loadPageStyle(QWidget *widget, const QString &resourcePath)
    {
        QFile file(resourcePath);
        if (file.open(QIODevice::ReadOnly | QIODevice::Text))
        {
            widget->setStyleSheet(QString::fromUtf8(file.readAll()));
        }
    }
}

WelcomePage::WelcomePage(QWidget *parent)
    : QWidget(parent)
{
    setObjectName(QStringLiteral("welcomePage"));
    setAttribute(Qt::WA_StyledBackground, true);
    // 欢迎窗口需要保持紧凑的 400 × 500 尺寸。
    setMinimumSize(0, 0);

    // 中央内容纵向排列，所有元素水平居中。
    auto *rootLayout = new QVBoxLayout(this);
    rootLayout->setContentsMargins(18, 12, 24, 24);
    rootLayout->setSizeConstraint(QLayout::SetNoConstraint);
    rootLayout->setSpacing(0);

    // 仅保留窗口控制按钮，不再显示独立的顶部导航栏/标题栏。
    auto *windowControls = new QWidget(this);
    auto *controlsLayout = new QHBoxLayout(windowControls);
    controlsLayout->setContentsMargins(0, 0, 0, 0);
    controlsLayout->setSpacing(8);
    const auto makeControl = [this, windowControls](const QString &text, const QString &name)
    {
        auto *button = new QPushButton(text, windowControls);
        button->setObjectName(name);
        button->setFixedSize(30, 30);
        button->setCursor(Qt::PointingHandCursor);
        button->setStyleSheet(QStringLiteral(
            "QPushButton { color: #625A7D; background: rgba(255,255,255,180); "
            "border: 1px solid rgba(109,88,172,45); border-radius: 15px; font-size: 15px; }"
            "QPushButton:hover { background: #E9E3FF; }"));
        return button;
    };
    auto *minButton = makeControl(QStringLiteral("−"), QStringLiteral("windowMinButton"));
    auto *maxButton = makeControl(QStringLiteral("□"), QStringLiteral("windowMaxButton"));
    auto *closeButton = makeControl(QStringLiteral("×"), QStringLiteral("windowCloseButton"));
    controlsLayout->addStretch(1);
    controlsLayout->addWidget(minButton);
    controlsLayout->addWidget(maxButton);
    controlsLayout->addWidget(closeButton);
    rootLayout->addWidget(windowControls, 0, Qt::AlignRight | Qt::AlignTop);

    connect(minButton, &QPushButton::clicked, this, [this]() { window()->showMinimized(); });
    connect(maxButton, &QPushButton::clicked, this, [this]()
    {
        if (window()->isMaximized()) window()->showNormal();
        else window()->showMaximized();
    });
    connect(closeButton, &QPushButton::clicked, this, [this]() { window()->close(); });

    auto *logo = new QLabel(QStringLiteral("G"), this);
    logo->setObjectName(QStringLiteral("welcomeMark"));
    logo->setAlignment(Qt::AlignCenter);
    logo->setFixedSize(70, 70);

    auto *title = new QLabel(QStringLiteral("Go聊"), this);
    title->setObjectName(QStringLiteral("welcomeTitle"));
    title->setAlignment(Qt::AlignCenter);

    auto *slogan = new QLabel(QStringLiteral("说 Go 就 Go"), this);
    slogan->setObjectName(QStringLiteral("welcomeSlogan"));
    slogan->setAlignment(Qt::AlignCenter);

    auto *description = new QLabel(
        QStringLiteral("让每一次沟通，都轻松一点。\n好友、群聊与灵感，都从这里开始。"), this);
    description->setObjectName(QStringLiteral("welcomeDescription"));
    description->setAlignment(Qt::AlignCenter);
    description->setWordWrap(true);

    m_enterButton = new QPushButton(QStringLiteral("进入  ↗"), this);
    m_enterButton->setObjectName(QStringLiteral("welcomeEnterButton"));
    m_enterButton->setCursor(Qt::PointingHandCursor);
    m_enterButton->setFixedSize(160, 48);

    auto *footnote = new QLabel(QStringLiteral("轻松连接 · 自在表达 · Go聊"), this);
    footnote->setObjectName(QStringLiteral("welcomeFootnote"));
    footnote->setAlignment(Qt::AlignCenter);

    rootLayout->addStretch(1);
    rootLayout->addWidget(logo, 0, Qt::AlignHCenter);
    rootLayout->addSpacing(22);
    rootLayout->addWidget(title, 0, Qt::AlignHCenter);
    rootLayout->addSpacing(8);
    rootLayout->addWidget(slogan, 0, Qt::AlignHCenter);
    rootLayout->addSpacing(15);
    rootLayout->addWidget(description, 0, Qt::AlignHCenter);
    rootLayout->addSpacing(32);
    rootLayout->addWidget(m_enterButton, 0, Qt::AlignHCenter);
    rootLayout->addStretch(1);
    rootLayout->addWidget(footnote, 0, Qt::AlignHCenter);

    // 按钮只负责发出信号，不直接依赖 MainWindow。
    connect(m_enterButton, &QPushButton::clicked,
            this, &WelcomePage::enterRequested);

    loadPageStyle(this, QStringLiteral(":/styles/WelcomePage.qss"));
}

void WelcomePage::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event);

    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);

    // 页面底色和两处径向光晕，对应 HTML 的 --canvas-gradient。
    painter.fillRect(rect(), QColor(QStringLiteral("#F5F5FA")));

    QRadialGradient upperGlow(
        QPointF(width() * 0.18, height() * 0.12),
        qMax(width(), height()) * 0.42);
    upperGlow.setColorAt(0.0, QColor(174, 159, 255, 58));
    upperGlow.setColorAt(0.46, QColor(201, 190, 255, 26));
    upperGlow.setColorAt(1.0, QColor(201, 190, 255, 0));
    painter.fillRect(rect(), upperGlow);

    QRadialGradient lowerGlow(
        QPointF(width() * 0.90, height() * 0.82),
        qMax(width(), height()) * 0.34);
    lowerGlow.setColorAt(0.0, QColor(201, 190, 255, 26));
    lowerGlow.setColorAt(1.0, QColor(201, 190, 255, 0));
    painter.fillRect(rect(), lowerGlow);
}
