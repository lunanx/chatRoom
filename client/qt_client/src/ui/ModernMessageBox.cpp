#include "ModernMessageBox.h"

#include "Theme.h"

#include <QCloseEvent>
#include <QCursor>
#include <QFont>
#include <QFontMetrics>
#include <QGraphicsDropShadowEffect>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QPushButton>
#include <QScreen>
#include <QShowEvent>
#include <QVBoxLayout>
#include <QWidget>

void ModernMessageBox::showMessage(QWidget *parent,
                                   const QString &title,
                                   const QString &message,
                                   bool isError)
{
    // 使用局部对象管理提示框生命周期；exec() 会在用户关闭提示框前保持模态事件循环。
    ModernMessageBox messageBox(parent, title, message, isError);
    messageBox.exec();
}

ModernMessageBox::ModernMessageBox(QWidget *parent,
                                   const QString &title,
                                   const QString &message,
                                   bool isError)
    : QDialog(parent)
{
    // 不使用操作系统标题栏和边框，并让窗口外部区域透明，以便显示卡片阴影。
    setWindowFlags(Qt::Dialog | Qt::FramelessWindowHint);
    setAttribute(Qt::WA_TranslucentBackground, true);
    setModal(true);
    setWindowTitle(title);
    setObjectName(QStringLiteral("modernMessageBox"));

    buildInterface(title, message, isError);
}

void ModernMessageBox::buildInterface(const QString &title,
                                      const QString &message,
                                      bool isError)
{
    // 外层只负责留出阴影空间；背景和圆角由内部卡片绘制。
    auto *outerLayout = new QVBoxLayout(this);
    outerLayout->setContentsMargins(22, 22, 22, 22);
    outerLayout->setSpacing(0);

    m_card = new QWidget(this);
    m_card->setObjectName(QStringLiteral("messageCard"));
    m_card->setAttribute(Qt::WA_StyledBackground, true);
    m_card->setStyleSheet(QStringLiteral(
        "QWidget#messageCard {"
        "  background-color: %1;"
        "  border: 1px solid rgba(109, 88, 172, 45);"
        "  border-radius: 15px;"
        "}"
    ).arg(Theme::Surface.name()));

    // 柔和的紫灰阴影与主界面的阴影颜色保持一致。
    auto *shadow = new QGraphicsDropShadowEffect(m_card);
    shadow->setBlurRadius(28.0);
    shadow->setOffset(0.0, 7.0);
    shadow->setColor(Theme::WindowShadowColor);
    m_card->setGraphicsEffect(shadow);

    auto *cardLayout = new QVBoxLayout(m_card);
    cardLayout->setContentsMargins(26, 24, 26, 22);
    cardLayout->setSpacing(0);

    // 提示标题使用稍大的加粗字体，错误与普通提示都保持紫灰主题。
    m_titleLabel = new QLabel(title, m_card);
    m_titleLabel->setObjectName(QStringLiteral("messageTitle"));
    m_titleLabel->setWordWrap(true);
    QFont titleFont = m_titleLabel->font();
    titleFont.setPointSize(15);
    titleFont.setWeight(QFont::DemiBold);
    m_titleLabel->setFont(titleFont);
    m_titleLabel->setStyleSheet(QStringLiteral(
        "QLabel#messageTitle { color: %1; background: transparent; border: none; }"
    ).arg(isError ? Theme::PurpleDeep.name() : Theme::Ink.name()));

    // 正文支持自动换行，并使用相对柔和的文字颜色。
    m_messageLabel = new QLabel(message, m_card);
    m_messageLabel->setObjectName(QStringLiteral("messageBody"));
    m_messageLabel->setWordWrap(true);
    m_messageLabel->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    m_messageLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_messageLabel->setStyleSheet(QStringLiteral(
        "QLabel#messageBody { color: %1; background: transparent; border: none; font-size: 13px; }"
    ).arg(Theme::InkSoft.name()));

    // 固定窗口宽度后，用字体度量计算正文需要的高度，确保较长报错也能自动换行显示。
    constexpr int kDialogWidth = 440;
    constexpr int kOuterMargins = 44; // 外层左右各 22px。
    constexpr int kCardMargins = 52;  // 卡片左右各 26px。
    const int textWidth = kDialogWidth - kOuterMargins - kCardMargins;
    m_messageLabel->setFixedWidth(textWidth);
    const QFontMetrics messageMetrics(m_messageLabel->font());
    const QRect messageBounds = messageMetrics.boundingRect(
        QRect(0, 0, textWidth, 10000),
        Qt::TextWordWrap,
        message);
    m_messageLabel->setFixedHeight(qMax(22, messageBounds.height() + 4));

    // 底部只提供一个确认按钮，颜色和主界面的紫色主按钮一致。
    m_confirmButton = new QPushButton(QStringLiteral("我知道了"), m_card);
    m_confirmButton->setObjectName(QStringLiteral("confirmButton"));
    m_confirmButton->setCursor(Qt::PointingHandCursor);
    m_confirmButton->setFixedSize(108, 40);
    m_confirmButton->setDefault(true);
    m_confirmButton->setStyleSheet(QStringLiteral(
        "QPushButton#confirmButton {"
        "  color: #FFFFFF; background-color: %1; border: none;"
        "  border-radius: 12px; padding: 0 18px; font-size: 13px; font-weight: 600;"
        "}"
        "QPushButton#confirmButton:hover { background-color: %2; }"
        "QPushButton#confirmButton:pressed { background-color: #4935C8; }"
        "QPushButton#confirmButton:focus { outline: none; border: 1px solid rgba(106,83,246,100); }"
    ).arg(Theme::Purple.name(), Theme::PurpleDeep.name()));

    cardLayout->addWidget(m_titleLabel);
    cardLayout->addSpacing(12);
    cardLayout->addWidget(m_messageLabel);
    cardLayout->addSpacing(22);

    auto *buttonLayout = new QHBoxLayout;
    buttonLayout->setContentsMargins(0, 0, 0, 0);
    buttonLayout->addStretch(1);
    buttonLayout->addWidget(m_confirmButton);
    cardLayout->addLayout(buttonLayout);

    outerLayout->addWidget(m_card);

    // 卡片宽度固定；高度由标题、正文和按钮共同决定。
    m_card->setFixedWidth(kDialogWidth - kOuterMargins);
    const int cardHeight = cardLayout->sizeHint().height();
    setFixedSize(kDialogWidth, cardHeight + kOuterMargins);

    connect(m_confirmButton, &QPushButton::clicked,
            this, &ModernMessageBox::closeFromConfirmButton);
}

void ModernMessageBox::showEvent(QShowEvent *event)
{
    QDialog::showEvent(event);
    centerOnParentOrScreen();
}

void ModernMessageBox::centerOnParentOrScreen()
{
    // 如果父窗口正显示，就在其中心定位；这也适用于无边框的主窗口。
    QWidget *anchor = parentWidget() ? parentWidget()->window() : nullptr;
    if (anchor != nullptr && anchor->isVisible())
    {
        // mapToGlobal 可兼容父窗口自身位于副屏或采用无边框窗口的情况。
        const QPoint center = anchor->mapToGlobal(anchor->rect().center());
        move(center.x() - width() / 2,
             center.y() - height() / 2);
        return;
    }

    // 没有可见父窗口时，优先在鼠标所在屏幕居中；无法获取时退回主屏幕。
    QScreen *screen = QGuiApplication::screenAt(QCursor::pos());
    if (screen == nullptr)
    {
        screen = QGuiApplication::primaryScreen();
    }

    if (screen != nullptr)
    {
        const QRect available = screen->availableGeometry();
        move(available.center().x() - width() / 2,
             available.center().y() - height() / 2);
    }
}

void ModernMessageBox::closeFromConfirmButton()
{
    // 记录用户明确点击了按钮，然后执行 QDialog 标准的接受/关闭流程。
    m_confirmButtonClicked = true;
    QDialog::accept();
}

void ModernMessageBox::reject()
{
    // 不允许 Esc 或 QDialog 默认的 reject 途径关闭窗口。
    // 关闭提示框的唯一界面入口是“我知道了”按钮。
}

void ModernMessageBox::closeEvent(QCloseEvent *event)
{
    if (m_confirmButtonClicked)
    {
        event->accept();
    }
    else
    {
        event->ignore();
    }
}

void ModernMessageBox::keyPressEvent(QKeyEvent *event)
{
    if (event->key() == Qt::Key_Escape)
    {
        event->ignore();
        return;
    }

    QDialog::keyPressEvent(event);
}
