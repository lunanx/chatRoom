#include "SideBar.h"

#include <QAbstractButton>
#include <QButtonGroup>
#include <QFile>
#include <QLabel>
#include <QToolButton>
#include <QVBoxLayout>
#include <QWidget>
#include <QStyle>
#include <QDebug>
#include <QStringList>

// 导航栏尺寸、间距与 HTML 的 .nav-rail、.nav-main、.nav-bottom 对齐。
namespace
{
    constexpr int kSideBarWidth = 56;
    constexpr int kNavButtonSize = 36;
    constexpr int kAvatarSize = 30;
    constexpr int kMainNavTop = 66;
    constexpr int kNavSpacing = 8;
}

SideBar::SideBar(QWidget *parent)
    : QWidget(parent),
      m_mainButtonGroup(new QButtonGroup(this))
{
    setObjectName(QStringLiteral("sideBar"));
    setFixedWidth(kSideBarWidth);

    // 关键：让 QWidget 主动绘制 QSS 指定的背景；否则渐变背景可能不显示。
    setAttribute(Qt::WA_StyledBackground, true);

    // QSS 从 Qt 资源文件读取，避免依赖程序运行时的当前工作目录。
    QFile styleFile(QStringLiteral(":/styles/SideBar.qss"));
    if (styleFile.open(QIODevice::ReadOnly | QIODevice::Text))
    {
        setStyleSheet(QString::fromUtf8(styleFile.readAll()));
    }
    else
    {
        // 找不到资源时保留明确日志，方便排查 .qrc 或 CMake 配置问题。
        qWarning() << "无法加载 SideBar QSS：:/styles/SideBar.qss";
    }

    // 导航栏从上到下排列：品牌 Logo、主导航、底部操作区。
    auto *rootLayout = new QVBoxLayout(this);
    rootLayout->setContentsMargins(0, 13, 0, 13);
    rootLayout->setSpacing(0);

    // 顶部品牌区：27px 的渐变 G 图标，下方是 Go聊文字。
    auto *brandWidget = new QWidget(this);
    brandWidget->setObjectName(QStringLiteral("brandLogo"));
    auto *brandLayout = new QVBoxLayout(brandWidget);
    brandLayout->setContentsMargins(0, 0, 0, 0);
    brandLayout->setSpacing(4);
    brandLayout->setAlignment(Qt::AlignHCenter | Qt::AlignTop);

    auto *brandMark = new QLabel(QStringLiteral("G"), brandWidget);
    brandMark->setObjectName(QStringLiteral("brandMark"));
    brandMark->setAlignment(Qt::AlignCenter);
    brandMark->setFixedSize(27, 27);

    auto *brandWord = new QLabel(QStringLiteral("Go聊"), brandWidget);
    brandWord->setObjectName(QStringLiteral("brandWord"));
    brandWord->setAlignment(Qt::AlignCenter);

    brandLayout->addWidget(brandMark, 0, Qt::AlignHCenter);
    brandLayout->addWidget(brandWord, 0, Qt::AlignHCenter);
    rootLayout->addWidget(brandWidget, 0, Qt::AlignHCenter);

    // 主导航：消息、联系人、收藏、文件。初始选中“消息”。
    auto *mainNavWidget = new QWidget(this);
    mainNavWidget->setObjectName(QStringLiteral("navMain"));
    auto *mainNavLayout = new QVBoxLayout(mainNavWidget);
    mainNavLayout->setContentsMargins(0, kMainNavTop - 13 - 27 - 4 - 11, 0, 0);
    // 上面的 top margin 用于让第一个图标从窗口顶部约 66px 处开始。
    mainNavLayout->setSpacing(kNavSpacing);
    mainNavLayout->setAlignment(Qt::AlignHCenter | Qt::AlignTop);

    m_mainButtonGroup->setExclusive(true);

    const QStringList iconTexts = {
        QStringLiteral("▤"), // 消息：与 HTML 样稿一致的列表符号
        QStringLiteral("♧"), // 联系人
        QStringLiteral("♡"), // 收藏
        QStringLiteral("▦")  // 文件
    };
    const QStringList toolTips = {
        QStringLiteral("消息"),
        QStringLiteral("联系人"),
        QStringLiteral("收藏"),
        QStringLiteral("文件")
    };
    const QStringList objectNames = {
        QStringLiteral("messagesButton"),
        QStringLiteral("contactsButton"),
        QStringLiteral("favoritesButton"),
        QStringLiteral("filesButton")
    };

    for (int i = 0; i < iconTexts.size(); ++i)
    {
        auto *button = createNavButton(iconTexts.at(i), toolTips.at(i), objectNames.at(i), mainNavWidget);
        button->setCheckable(true);
        m_mainButtonGroup->addButton(button, i);
        mainNavLayout->addWidget(button, 0, Qt::AlignHCenter);
    }

    // 先选中消息按钮。setCurrentSection 会同步更新内部索引和 QSS 状态。
    setCurrentSection(Messages);
    rootLayout->addWidget(mainNavWidget, 0, Qt::AlignHCenter);

    // 弹性空间把设置、更多和用户头像推到底部，与 HTML 的 margin-top:auto 对应。
    rootLayout->addStretch(1);

    auto *bottomWidget = new QWidget(this);
    bottomWidget->setObjectName(QStringLiteral("navBottom"));
    auto *bottomLayout = new QVBoxLayout(bottomWidget);
    bottomLayout->setContentsMargins(0, 0, 0, 0);
    bottomLayout->setSpacing(kNavSpacing);
    bottomLayout->setAlignment(Qt::AlignHCenter | Qt::AlignBottom);

    auto *settingsButton = createNavButton(QStringLiteral("⚙"),
                                           QStringLiteral("设置"),
                                           QStringLiteral("settingsButton"),
                                           bottomWidget);
    auto *moreButton = createNavButton(QStringLiteral("⋯"),
                                       QStringLiteral("更多"),
                                       QStringLiteral("moreButton"),
                                       bottomWidget);
    bottomLayout->addWidget(settingsButton, 0, Qt::AlignHCenter);
    bottomLayout->addWidget(moreButton, 0, Qt::AlignHCenter);

    // 当前先用“我”作为头像占位；后续可以用 QLabel::setPixmap() 设置真实头像。
    m_userAvatar = new QLabel(QStringLiteral("我"), bottomWidget);
    m_userAvatar->setObjectName(QStringLiteral("navUserAvatar"));
    m_userAvatar->setAlignment(Qt::AlignCenter);
    m_userAvatar->setFixedSize(kAvatarSize, kAvatarSize);
    bottomLayout->addWidget(m_userAvatar, 0, Qt::AlignHCenter);

    rootLayout->addWidget(bottomWidget, 0, Qt::AlignHCenter);

    // 只有主导航的点击会改变当前 Section；设置和更多暂时仅提供外观与提示。
    connect(m_mainButtonGroup, &QButtonGroup::idClicked,
            this, [this](int index)
    {
        setCurrentSection(index);
        emit sectionChanged(index);
    });
}

QToolButton *SideBar::createNavButton(const QString &text,
                                      const QString &toolTip,
                                      const QString &objectName,
                                      QWidget *parent)
{
    auto *button = new QToolButton(parent);
    button->setObjectName(objectName);
    button->setText(text);
    button->setToolTip(toolTip);
    button->setFixedSize(kNavButtonSize, kNavButtonSize);
    button->setCursor(Qt::PointingHandCursor);
    button->setFocusPolicy(Qt::NoFocus);
    button->setAutoRaise(false);
    return button;
}

void SideBar::setCurrentSection(int index)
{
    // 防止外部传入不存在的导航索引，避免错误的按钮状态。
    if (index < Messages || index > Files)
    {
        return;
    }

    QAbstractButton *button = m_mainButtonGroup->button(index);
    if (button == nullptr)
    {
        return;
    }

    m_currentSection = index;
    button->setChecked(true);

    // checked 属性会触发 QSS 的 :checked 规则，更新白色选中背景与紫色文字。
    // 主导航的其他按钮由 exclusive group 自动取消选中。
    for (QAbstractButton *item : m_mainButtonGroup->buttons())
    {
        auto *toolButton = qobject_cast<QToolButton *>(item);
        if (toolButton != nullptr)
        {
            refreshButtonStyle(toolButton);
        }
    }
}

int SideBar::currentSection() const
{
    return m_currentSection;
}

void SideBar::refreshButtonStyle(QToolButton *button)
{
    // Qt 对动态属性 / checked 状态的 QSS 缓存可能需要重新 polish 才能立即刷新。
    if (button == nullptr)
    {
        return;
    }

    button->style()->unpolish(button);
    button->style()->polish(button);
    button->update();
}
