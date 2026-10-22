#include "AuthPage.h"

#include <QCheckBox>
#include <QEvent>
#include <QFile>
#include <QGraphicsDropShadowEffect>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QVBoxLayout>
#include <QWidget>
#include <QColor>
#include <QPaintEvent>
#include <QPainter>
#include <QPainterPath>
#include <QSizePolicy>
#include <QLinearGradient>

namespace
{
    // 左侧品牌面板：用 QPainter 绘制渐变底色和半透明紫色圆环。
    class AuthBrandPanel : public QWidget
    {
    public:
        explicit AuthBrandPanel(QWidget *parent = nullptr)
            : QWidget(parent)
        {
            setObjectName(QStringLiteral("authBrandPanel"));
            setAttribute(Qt::WA_StyledBackground, false);
        }

    protected:
        void paintEvent(QPaintEvent *event) override
        {
            Q_UNUSED(event);
            QPainter painter(this);
            painter.setRenderHint(QPainter::Antialiasing, true);

            // 裁剪左侧两个外角，避免品牌渐变覆盖卡片的圆角。
            QPainterPath clipPath;
            clipPath.moveTo(31, 0);
            clipPath.lineTo(width(), 0);
            clipPath.lineTo(width(), height());
            clipPath.lineTo(31, height());
            clipPath.quadTo(0, height(), 0, height() - 31);
            clipPath.lineTo(0, 31);
            clipPath.quadTo(0, 0, 31, 0);
            painter.setClipPath(clipPath);

            QLinearGradient background(rect().topLeft(), rect().bottomRight());
            background.setColorAt(0.0, QColor(QStringLiteral("#F1EDFF")));
            background.setColorAt(0.58, QColor(QStringLiteral("#E9E3FB")));
            background.setColorAt(1.0, QColor(QStringLiteral("#F8F5FF")));
            painter.fillRect(rect(), background);

            // 右上方的大圆环及其两道淡色外圈。
            const QPointF upperCenter(width() + 20, -55);
            painter.setBrush(Qt::NoBrush);
            painter.setPen(QPen(QColor(106, 83, 246, 31), 1));
            painter.drawEllipse(upperCenter, 210, 210);
            painter.setPen(QPen(QColor(106, 83, 246, 9), 35));
            painter.drawEllipse(upperCenter, 210, 210);
            painter.setPen(QPen(QColor(106, 83, 246, 6), 40));
            painter.drawEllipse(upperCenter, 250, 250);

            // 左下方的装饰圆环。
            const QPointF lowerCenter(-55, height() + 20);
            painter.setPen(QPen(QColor(106, 83, 246, 31), 1));
            painter.drawEllipse(lowerCenter, 125, 125);
            painter.setPen(QPen(QColor(106, 83, 246, 9), 26));
            painter.drawEllipse(lowerCenter, 125, 125);
        }
    };

    // 从 Qt 资源系统加载当前页面的 QSS。
    void loadPageStyle(QWidget *widget, const QString &resourcePath)
    {
        QFile file(resourcePath);
        if (file.open(QIODevice::ReadOnly | QIODevice::Text))
        {
            widget->setStyleSheet(QString::fromUtf8(file.readAll()));
        }
    }

    // 创建带标题的表单字段。
    QWidget *createField(const QString &labelText,
                         QLineEdit *edit,
                         QWidget *parent)
    {
        auto *field = new QWidget(parent);
        field->setObjectName(QStringLiteral("authFormField"));

        auto *layout = new QVBoxLayout(field);
        layout->setContentsMargins(0, 0, 0, 0);
        layout->setSpacing(9);

        auto *label = new QLabel(labelText, field);
        label->setObjectName(QStringLiteral("authFieldLabel"));

        layout->addWidget(label);
        layout->addWidget(edit);
        return field;
    }

    // 为输入框创建紫色外发光效果，默认不显示，获得焦点后再打开。
    void installFocusGlow(QLineEdit *edit)
    {
        auto *shadow = new QGraphicsDropShadowEffect(edit);
        shadow->setBlurRadius(0);
        shadow->setOffset(0, 0);
        shadow->setColor(QColor(106, 83, 246, 55));
        edit->setGraphicsEffect(shadow);
    }
}

AuthPage::AuthPage(QWidget *parent)
    : QWidget(parent)
{
    setObjectName(QStringLiteral("authPage"));
    setAttribute(Qt::WA_StyledBackground, true);
    // 页面最小尺寸交给 LoginWindow 控制，方便欢迎页缩小到 400 × 500。
    setMinimumSize(0, 0);

    // 卡片直接铺满窗口内部，只留少量空间显示圆角，不再出现外框。
    // 使用网格布局，让窗口控制按钮浮在卡片右上角，不额外占用表单空间。
    auto *pageLayout = new QGridLayout(this);
    pageLayout->setContentsMargins(6, 6, 6, 6);
    pageLayout->setSizeConstraint(QLayout::SetNoConstraint);
    pageLayout->setSpacing(0);

    // 登录/注册页也只保留窗口控制按钮，不显示系统标题栏。
    auto *windowControls = new QWidget(this);
    auto *controlsLayout = new QHBoxLayout(windowControls);
    controlsLayout->setContentsMargins(0, 0, 0, 0);
    controlsLayout->setSpacing(4);
    const auto makeControl = [windowControls](const QString &text, const QString &name)
    {
        auto *button = new QPushButton(text, windowControls);
        button->setObjectName(name);
        button->setFixedSize(22, 22);
        button->setCursor(Qt::PointingHandCursor);
        // 控制按钮直接融入右侧页面背景，不再使用白色圆形底和外边框。
        // 只有悬停时才显示很淡的背景反馈，避免破坏整体卡片效果。
        button->setStyleSheet(QStringLiteral(
            "QPushButton { color: #625A7D; background: transparent; "
            "border: none; border-radius: 11px; font-size: 12px; padding: 0; }"
            "QPushButton:hover { background: rgba(106,83,246,18); }"
            "QPushButton:pressed { background: rgba(106,83,246,30); }"));
        return button;
    };
    auto *minButton = makeControl(QStringLiteral("−"), QStringLiteral("windowMinButton"));
    auto *maxButton = makeControl(QStringLiteral("□"), QStringLiteral("windowMaxButton"));
    auto *closeButton = makeControl(QStringLiteral("×"), QStringLiteral("windowCloseButton"));
    controlsLayout->addStretch(1);
    controlsLayout->addWidget(minButton);
    controlsLayout->addWidget(maxButton);
    controlsLayout->addWidget(closeButton);

    connect(minButton, &QPushButton::clicked, this, [this]() { window()->showMinimized(); });
    connect(maxButton, &QPushButton::clicked, this, [this]()
    {
        if (window()->isMaximized()) window()->showNormal();
        else window()->showMaximized();
    });
    connect(closeButton, &QPushButton::clicked, this, [this]() { window()->close(); });

    auto *card = new QWidget(this);
    card->setObjectName(QStringLiteral("authCard"));
    // 卡片宽高由表单内容和窗口尺寸决定，不再强制撑大欢迎页窗口。
    card->setMinimumSize(0, 0);
    card->setMaximumWidth(1120);

    auto *cardLayout = new QHBoxLayout(card);
    cardLayout->setContentsMargins(0, 0, 0, 0);
    cardLayout->setSpacing(0);

    // 左侧品牌介绍区。
    auto *brandPanel = new AuthBrandPanel(card);
    brandPanel->setMinimumWidth(280);
    brandPanel->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Expanding);

    auto *brandLayout = new QVBoxLayout(brandPanel);
    brandLayout->setContentsMargins(40, 36, 36, 30);
    brandLayout->setSpacing(0);

    auto *miniBrandRow = new QHBoxLayout();
    miniBrandRow->setSpacing(10);

    auto *miniMark = new QLabel(QStringLiteral("G"), brandPanel);
    miniMark->setObjectName(QStringLiteral("authMiniMark"));
    miniMark->setAlignment(Qt::AlignCenter);
    miniMark->setFixedSize(33, 33);

    auto *miniName = new QLabel(QStringLiteral("Go聊"), brandPanel);
    miniName->setObjectName(QStringLiteral("authMiniName"));

    miniBrandRow->addWidget(miniMark);
    miniBrandRow->addWidget(miniName);
    miniBrandRow->addStretch(1);
    brandLayout->addLayout(miniBrandRow);

    brandLayout->addStretch(1);

    auto *brandTitle = new QLabel(
        QStringLiteral("让聊天\n回归轻松。"), brandPanel);
    brandTitle->setObjectName(QStringLiteral("authBrandTitle"));
    brandTitle->setWordWrap(true);

    auto *brandSlogan = new QLabel(
        QStringLiteral("说 Go 就 Go"), brandPanel);
    brandSlogan->setObjectName(QStringLiteral("authBrandSlogan"));

    auto *brandDescription = new QLabel(
        QStringLiteral("把重要的人和对话放在身边。简单、清爽，也留有一点温柔的紫色空间。"),
        brandPanel);
    brandDescription->setObjectName(QStringLiteral("authBrandDescription"));
    brandDescription->setWordWrap(true);

    brandLayout->addWidget(brandTitle);
    brandLayout->addSpacing(19);
    brandLayout->addWidget(brandSlogan);
    brandLayout->addSpacing(17);
    brandLayout->addWidget(brandDescription);
    brandLayout->addStretch(1);

    auto *brandFooterRow = new QHBoxLayout();
    brandFooterRow->setSpacing(8);

    auto *brandDot = new QLabel(QStringLiteral("●"), brandPanel);
    brandDot->setObjectName(QStringLiteral("authBrandDot"));
    auto *brandFooter = new QLabel(
        QStringLiteral("每一次连接，都值得被认真对待"), brandPanel);
    brandFooter->setObjectName(QStringLiteral("authBrandFooter"));

    brandFooterRow->addWidget(brandDot);
    brandFooterRow->addWidget(brandFooter);
    brandFooterRow->addStretch(1);
    brandLayout->addLayout(brandFooterRow);

    // 右侧表单区。
    auto *formPanel = new QWidget(card);
    formPanel->setObjectName(QStringLiteral("authFormPanel"));
    formPanel->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);

    auto *formPanelLayout = new QVBoxLayout(formPanel);
    formPanelLayout->setContentsMargins(48, 38, 48, 34);
    formPanelLayout->setAlignment(Qt::AlignCenter);

    auto *form = new QWidget(formPanel);
    form->setObjectName(QStringLiteral("authForm"));
    form->setMaximumWidth(360);

    auto *formLayout = new QVBoxLayout(form);
    formLayout->setContentsMargins(0, 0, 0, 0);
    formLayout->setSpacing(0);

    auto *eyebrow = new QLabel(QStringLiteral("WELCOME BACK"), form);
    eyebrow->setObjectName(QStringLiteral("authFormEyebrow"));

    auto *formTitle = new QLabel(QStringLiteral("欢迎来到 Go聊"), form);
    formTitle->setObjectName(QStringLiteral("authFormTitle"));

    auto *subtitle = new QLabel(
        QStringLiteral("登录后继续和朋友保持联系。"), form);
    subtitle->setObjectName(QStringLiteral("authFormSubtitle"));
    subtitle->setWordWrap(true);

    m_accountEdit = new QLineEdit(form);
    m_accountEdit->setObjectName(QStringLiteral("authAccountEdit"));
    m_accountEdit->setPlaceholderText(QStringLiteral("请输入账号"));
    m_accountEdit->setClearButtonEnabled(true);

    m_passwordEdit = new QLineEdit(form);
    m_passwordEdit->setObjectName(QStringLiteral("authPasswordEdit"));
    m_passwordEdit->setPlaceholderText(QStringLiteral("请输入密码"));
    m_passwordEdit->setEchoMode(QLineEdit::Password);

    // 加上焦点阴影效果；事件过滤器控制它的显示和隐藏。
    installFocusGlow(m_accountEdit);
    installFocusGlow(m_passwordEdit);
    m_accountEdit->installEventFilter(this);
    m_passwordEdit->installEventFilter(this);

    m_rememberCheckBox = new QCheckBox(QStringLiteral("记住我"), form);
    m_rememberCheckBox->setObjectName(QStringLiteral("authRememberCheckBox"));
    m_rememberCheckBox->setChecked(true);

    auto *hintRow = new QHBoxLayout();
    hintRow->setContentsMargins(0, 0, 0, 24);
    hintRow->addWidget(m_rememberCheckBox);
    hintRow->addStretch(1);

    m_loginButton = new QPushButton(QStringLiteral("登录"), form);
    m_loginButton->setObjectName(QStringLiteral("authLoginButton"));
    m_loginButton->setCursor(Qt::PointingHandCursor);

    m_registerButton = new QPushButton(QStringLiteral("注册账号"), form);
    m_registerButton->setObjectName(QStringLiteral("authRegisterButton"));
    m_registerButton->setCursor(Qt::PointingHandCursor);

    auto *actions = new QHBoxLayout();
    actions->setContentsMargins(0, 0, 0, 0);
    actions->setSpacing(10);
    actions->addWidget(m_loginButton, 1);
    actions->addWidget(m_registerButton, 1);

    auto *legal = new QLabel(
        QStringLiteral("继续使用即表示你同意我们的服务条款与隐私政策。"), form);
    legal->setObjectName(QStringLiteral("authFormLegal"));
    legal->setWordWrap(true);
    legal->setAlignment(Qt::AlignCenter);

    formLayout->addWidget(eyebrow);
    formLayout->addSpacing(9);
    formLayout->addWidget(formTitle);
    formLayout->addSpacing(10);
    formLayout->addWidget(subtitle);
    formLayout->addSpacing(28);
    formLayout->addWidget(createField(QStringLiteral("账号"), m_accountEdit, form));
    formLayout->addSpacing(18);
    formLayout->addWidget(createField(QStringLiteral("密码"), m_passwordEdit, form));
    formLayout->addSpacing(18);
    formLayout->addLayout(hintRow);
    formLayout->addLayout(actions);
    formLayout->addSpacing(23);
    formLayout->addWidget(legal);

    cardLayout->addWidget(brandPanel, 43);
    cardLayout->addWidget(formPanel, 57);
    formPanelLayout->addWidget(form);

    // 卡片铺满页面；控制按钮覆盖在右上角，靠右留出 10px 内边距。
    pageLayout->addWidget(card, 0, 0);
    pageLayout->addWidget(windowControls, 0, 0, Qt::AlignRight | Qt::AlignTop);
    pageLayout->setAlignment(windowControls, Qt::AlignRight | Qt::AlignTop);
    windowControls->raise();
    windowControls->setContentsMargins(0, 2, 4, 0);

    connect(m_loginButton, &QPushButton::clicked,
            this, [this]()
    {
        emit loginRequested(m_accountEdit->text().trimmed(),
                            m_passwordEdit->text(),
                            m_rememberCheckBox->isChecked());
    });

    connect(m_registerButton, &QPushButton::clicked,
            this, [this]()
    {
        emit registerRequested(m_accountEdit->text().trimmed(),
                               m_passwordEdit->text());
    });

    loadPageStyle(this, QStringLiteral(":/styles/AuthPage.qss"));
}

void AuthPage::focusAccountInput()
{
    m_accountEdit->setFocus();
}

bool AuthPage::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == m_accountEdit || watched == m_passwordEdit)
    {
        auto *edit = qobject_cast<QLineEdit *>(watched);
        auto *shadow = qobject_cast<QGraphicsDropShadowEffect *>(edit->graphicsEffect());

        if (shadow != nullptr)
        {
            if (event->type() == QEvent::FocusIn)
            {
                // 对应 HTML :focus 的紫色外发光与边框。
                shadow->setBlurRadius(16);
                shadow->setColor(QColor(106, 83, 246, 55));
            }
            else if (event->type() == QEvent::FocusOut)
            {
                shadow->setBlurRadius(0);
            }
        }
    }

    return QWidget::eventFilter(watched, event);
}
