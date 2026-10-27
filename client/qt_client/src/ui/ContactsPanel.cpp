#include "ContactsPanel.h"

#include <QDateTime>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QStyle>
#include <QTabWidget>
#include <QVBoxLayout>
#include <QWidget>

namespace
{
    QString contactDisplayName(const QString &nickname, const QString &username)
    {
        const QString normalizedNickname = nickname.trimmed();
        return normalizedNickname.isEmpty() ? username : normalizedNickname;
    }

    QString displayCreatedAt(const QString &value)
    {
        if (value.trimmed().isEmpty())
        {
            return {};
        }
        const QDateTime time = QDateTime::fromString(value, QStringLiteral("yyyy-MM-dd HH:mm:ss"));
        return time.isValid() ? time.toString(QStringLiteral("yyyy/MM/dd")) : value;
    }

    QWidget *makeAvatar(const QString &displayName, QWidget *parent)
    {
        const QString avatarText = displayName.isEmpty()
                                       ? QStringLiteral("?")
                                       : displayName.left(1);
        auto *avatar = new QLabel(avatarText, parent);
        avatar->setObjectName(QStringLiteral("contactAvatar"));
        avatar->setAlignment(Qt::AlignCenter);
        avatar->setFixedSize(42, 42);
        avatar->setStyleSheet(QStringLiteral(
            "QLabel#contactAvatar { color: white; background: #8B78EF;"
            " border-radius: 21px; font-size: 15px; font-weight: 700; }"));
        return avatar;
    }

    QPushButton *makeActionButton(const QString &text, const QString &objectName, QWidget *parent)
    {
        auto *button = new QPushButton(text, parent);
        button->setObjectName(objectName);
        button->setCursor(Qt::PointingHandCursor);
        button->setFixedHeight(30);
        return button;
    }
}

ContactsPanel::ContactsPanel(QWidget *parent)
    : QWidget(parent)
{
    setObjectName(QStringLiteral("contactsPanel"));
    setAttribute(Qt::WA_StyledBackground, true);
    setStyleSheet(QStringLiteral(
        "QWidget#contactsPanel { background: rgba(252,251,255,238); color: #302A48; }"
        "QLabel#contactsTitle { color: #302A48; font-size: 24px; font-weight: 800; }"
        "QLabel#contactsDescription { color: #8F87A8; font-size: 12px; }"
        "QLineEdit#friendUsernameEdit { color: #302A48; background: #F6F3FC;"
        " border: 1px solid #DED7F0; border-radius: 10px; padding: 0 12px; min-height: 38px; }"
        "QLineEdit#friendUsernameEdit:focus { border: 1px solid #8A76EF; background: #FFFFFF; }"
        "QPushButton#addFriendButton, QPushButton#contactPrimaryButton { color: #FFFFFF;"
        " background: #6A53F6; border: none; border-radius: 9px; padding: 0 14px; font-weight: 650; }"
        "QPushButton#addFriendButton:hover, QPushButton#contactPrimaryButton:hover { background: #5941E7; }"
        "QPushButton#contactRefreshButton, QPushButton#contactSecondaryButton { color: #625879;"
        " background: #F0EDFA; border: 1px solid #E4DDF3; border-radius: 9px; padding: 0 12px; }"
        "QPushButton#contactRefreshButton:hover, QPushButton#contactSecondaryButton:hover { background: #E8E1FB; }"
        "QPushButton#contactDangerButton { color: #A74769; background: #FCECF2; border: 1px solid #F5D9E4;"
        " border-radius: 9px; padding: 0 12px; }"
        "QPushButton#contactDangerButton:hover { background: #F8DDE8; }"
        "QLabel#contactsStatus { color: #817896; font-size: 12px; padding: 2px 2px; }"
        "QLabel#contactsStatus[error=\"true\"] { color: #B34E73; }"
        "QTabWidget::pane { border: 1px solid #EBE5F5; border-radius: 12px; background: #FFFFFF; top: -1px; }"
        "QTabBar::tab { color: #89819F; background: transparent; padding: 11px 18px; margin-right: 6px;"
        " border-bottom: 2px solid transparent; font-weight: 650; }"
        "QTabBar::tab:selected { color: #6A53F6; border-bottom: 2px solid #6A53F6; }"
        "QListWidget { background: transparent; border: none; outline: none; padding: 8px; }"
        "QListWidget::item { border: none; background: transparent; }"
        "QListWidget::item:selected { background: #F3EFFF; border-radius: 10px; }"));

    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(30, 30, 30, 26);
    root->setSpacing(16);

    auto *headingRow = new QHBoxLayout;
    auto *headingColumn = new QVBoxLayout;
    headingColumn->setSpacing(5);
    auto *title = new QLabel(QStringLiteral("联系人"), this);
    title->setObjectName(QStringLiteral("contactsTitle"));
    auto *description = new QLabel(QStringLiteral("管理好友关系，并从好友列表直接发起私聊。"), this);
    description->setObjectName(QStringLiteral("contactsDescription"));
    headingColumn->addWidget(title);
    headingColumn->addWidget(description);
    auto *refreshButton = makeActionButton(QStringLiteral("刷新"), QStringLiteral("contactRefreshButton"), this);
    connect(refreshButton, &QPushButton::clicked, this, &ContactsPanel::refreshRequested);
    headingRow->addLayout(headingColumn, 1);
    headingRow->addWidget(refreshButton, 0, Qt::AlignVCenter);
    root->addLayout(headingRow);

    auto *addRow = new QHBoxLayout;
    addRow->setSpacing(10);
    m_usernameEdit = new QLineEdit(this);
    m_usernameEdit->setObjectName(QStringLiteral("friendUsernameEdit"));
    m_usernameEdit->setPlaceholderText(QStringLiteral("输入对方用户名，例如 alice_01"));
    m_usernameEdit->setClearButtonEnabled(true);
    m_addButton = makeActionButton(QStringLiteral("添加好友"), QStringLiteral("addFriendButton"), this);
    m_addButton->setMinimumWidth(106);
    connect(m_addButton, &QPushButton::clicked, this, &ContactsPanel::submitFriendRequest);
    connect(m_usernameEdit, &QLineEdit::returnPressed,
            this, &ContactsPanel::submitFriendRequest);
    addRow->addWidget(m_usernameEdit, 1);
    addRow->addWidget(m_addButton);
    root->addLayout(addRow);

    m_statusLabel = new QLabel(QStringLiteral("好友列表和收到的申请来自服务器。"), this);
    m_statusLabel->setObjectName(QStringLiteral("contactsStatus"));
    m_statusLabel->setWordWrap(true);
    m_statusLabel->setProperty("error", false);
    root->addWidget(m_statusLabel);

    m_tabs = new QTabWidget(this);
    m_friendList = new QListWidget(m_tabs);
    m_friendList->setObjectName(QStringLiteral("contactFriendList"));
    m_friendList->setSpacing(5);
    m_requestList = new QListWidget(m_tabs);
    m_requestList->setObjectName(QStringLiteral("pendingFriendRequestList"));
    m_requestList->setSpacing(5);
    m_tabs->addTab(m_friendList, QStringLiteral("好友 (0)"));
    m_tabs->addTab(m_requestList, QStringLiteral("收到的申请 (0)"));
    root->addWidget(m_tabs, 1);
}

void ContactsPanel::setFriends(const QVector<ContactItem> &friends)
{
    m_friends = friends;
    rebuildFriendRows();
    m_tabs->setTabText(0, QStringLiteral("好友 (%1)").arg(m_friends.size()));
}

void ContactsPanel::setPendingRequests(const QVector<PendingFriendRequestItem> &requests)
{
    m_pendingRequests = requests;
    rebuildRequestRows();
    m_tabs->setTabText(1, QStringLiteral("收到的申请 (%1)").arg(m_pendingRequests.size()));
}

void ContactsPanel::setStatus(const QString &message, bool isError)
{
    m_statusLabel->setText(message);
    m_statusLabel->setProperty("error", isError);
    m_statusLabel->style()->unpolish(m_statusLabel);
    m_statusLabel->style()->polish(m_statusLabel);
    m_statusLabel->update();
}

void ContactsPanel::finishAddFriendRequest(bool clearInput)
{
    m_addButton->setEnabled(true);
    if (clearInput)
    {
        m_usernameEdit->clear();
    }
}

void ContactsPanel::submitFriendRequest()
{
    const QString username = m_usernameEdit->text().trimmed();
    if (username.isEmpty())
    {
        setStatus(QStringLiteral("请输入要添加的用户名。"), true);
        m_usernameEdit->setFocus();
        return;
    }

    m_addButton->setEnabled(false);
    emit sendFriendRequestRequested(username);
}

void ContactsPanel::rebuildFriendRows()
{
    m_friendList->clear();
    if (m_friends.isEmpty())
    {
        auto *empty = new QListWidgetItem(QStringLiteral("还没有好友。可以在上方输入用户名发送申请。"), m_friendList);
        empty->setFlags(Qt::NoItemFlags);
        empty->setSizeHint(QSize(280, 64));
        return;
    }

    for (const ContactItem &contact : m_friends)
    {
        auto *item = new QListWidgetItem(m_friendList);
        auto *row = new QWidget(m_friendList);
        auto *layout = new QHBoxLayout(row);
        layout->setContentsMargins(10, 7, 10, 7);
        layout->setSpacing(11);
        const QString displayName = contactDisplayName(contact.nickname, contact.username);
        layout->addWidget(makeAvatar(displayName, row));

        auto *textColumn = new QVBoxLayout;
        textColumn->setSpacing(4);
        auto *nameLabel = new QLabel(displayName, row);
        nameLabel->setStyleSheet(QStringLiteral("font-size: 14px; font-weight: 700; color: #302A48;"));
        QString subText = QStringLiteral("@%1").arg(contact.username);
        const QString created = displayCreatedAt(contact.createdAt);
        if (!created.isEmpty())
        {
            subText += QStringLiteral(" · 成为好友于 ") + created;
        }
        auto *subLabel = new QLabel(subText, row);
        subLabel->setStyleSheet(QStringLiteral("font-size: 11px; color: #9188A8;"));
        textColumn->addWidget(nameLabel);
        textColumn->addWidget(subLabel);
        layout->addLayout(textColumn, 1);

        auto *openButton = makeActionButton(QStringLiteral("发消息"), QStringLiteral("contactPrimaryButton"), row);
        openButton->setFixedWidth(78);
        connect(openButton, &QPushButton::clicked, this, [this, userId = contact.userId]()
        {
            if (userId > 0)
            {
                emit openPrivateChatRequested(userId);
            }
        });
        layout->addWidget(openButton);
        item->setSizeHint(QSize(360, 66));
        m_friendList->setItemWidget(item, row);
    }
}

void ContactsPanel::rebuildRequestRows()
{
    m_requestList->clear();
    if (m_pendingRequests.isEmpty())
    {
        auto *empty = new QListWidgetItem(QStringLiteral("当前没有待处理的好友申请。"), m_requestList);
        empty->setFlags(Qt::NoItemFlags);
        empty->setSizeHint(QSize(280, 64));
        return;
    }

    for (const PendingFriendRequestItem &request : m_pendingRequests)
    {
        auto *item = new QListWidgetItem(m_requestList);
        auto *row = new QWidget(m_requestList);
        auto *layout = new QHBoxLayout(row);
        layout->setContentsMargins(10, 7, 10, 7);
        layout->setSpacing(11);
        const QString displayName = contactDisplayName(request.nickname, request.username);
        layout->addWidget(makeAvatar(displayName, row));

        auto *textColumn = new QVBoxLayout;
        textColumn->setSpacing(4);
        auto *nameLabel = new QLabel(displayName, row);
        nameLabel->setStyleSheet(QStringLiteral("font-size: 14px; font-weight: 700; color: #302A48;"));
        QString subText = QStringLiteral("@%1").arg(request.username);
        const QString created = displayCreatedAt(request.createdAt);
        if (!created.isEmpty())
        {
            subText += QStringLiteral(" · %1").arg(created);
        }
        auto *subLabel = new QLabel(subText, row);
        subLabel->setStyleSheet(QStringLiteral("font-size: 11px; color: #9188A8;"));
        textColumn->addWidget(nameLabel);
        textColumn->addWidget(subLabel);
        layout->addLayout(textColumn, 1);

        auto *acceptButton = makeActionButton(QStringLiteral("同意"), QStringLiteral("contactPrimaryButton"), row);
        auto *rejectButton = makeActionButton(QStringLiteral("拒绝"), QStringLiteral("contactDangerButton"), row);
        acceptButton->setFixedWidth(62);
        rejectButton->setFixedWidth(62);
        connect(acceptButton, &QPushButton::clicked, this,
                [this, requestId = request.requestId]()
        {
            // 服务端处理申请只需要申请记录 ID 和操作类型。
            // 申请人的 user_id 由 AppController 从服务器返回的数据中关联，
            // 不在按钮事件中作为必需参数传递，避免 UI 参数不一致时拦截同意操作。
            if (requestId > 0)
            {
                emit handleFriendRequestRequested(requestId, 1);
            }
        });
        connect(rejectButton, &QPushButton::clicked, this,
                [this, requestId = request.requestId]()
        {
            if (requestId > 0)
            {
                emit handleFriendRequestRequested(requestId, 2);
            }
        });
        layout->addWidget(acceptButton);
        layout->addWidget(rejectButton);
        item->setSizeHint(QSize(420, 66));
        m_requestList->setItemWidget(item, row);
    }
}
