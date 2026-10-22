#include "WelcomePage.h"

#include <QFile>
#include <QLabel>
#include <QPaintEvent>
#include <QPainter>
#include <QPushButton>
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
    setMinimumSize(480, 420);

    // 中央内容纵向排列，所有元素水平居中。
    auto *rootLayout = new QVBoxLayout(this);
    rootLayout->setContentsMargins(24, 34, 24, 34);
    rootLayout->setSpacing(0);
    rootLayout->setAlignment(Qt::AlignCenter);

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
