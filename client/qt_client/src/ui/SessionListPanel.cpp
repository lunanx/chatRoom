#include "SessionListPanel.h"

#include "../Theme.h"

#include <QAbstractItemView>
#include <QButtonGroup>
#include <QFont>
#include <QHBoxLayout>
#include <QLineEdit>
#include <QLabel>
#include <QFile>
#include <QIODevice>
#include <QSizePolicy>
#include <QListView>
#include <QPainter>
#include <QPainterPath>
#include <QPushButton>
#include <QVBoxLayout>
#include <QApplication>
#include <QStyle>
#include <QStyleOptionViewItem>
#include <QScrollBar>
#include <QTimer>

// ---------------------------- SessionListModel ----------------------------

SessionListModel::SessionListModel(QObject *parent)
    : QAbstractListModel(parent)
{
    // 初始为空；只有收到服务端会话列表后才展示真实会话。
}

int SessionListModel::rowCount(const QModelIndex &parent) const
{
    // 列表模型没有树形子节点；parent 有效时表示询问子项，返回 0。
    return parent.isValid() ? 0 : m_sessions.size();
}

QVariant SessionListModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_sessions.size())
        return {};

    const SessionItem &item = m_sessions.at(index.row());
    switch (role)
    {
    case Qt::DisplayRole:
    case NameRole:          return item.name;
    case IdRole:            return item.id;
    case TimeRole:          return item.time;
    case PreviewRole:       return item.preview;
    case AvatarTextRole:    return item.avatarText;
    case AvatarStartRole:   return item.avatarStart;
    case AvatarEndRole:     return item.avatarEnd;
    case UnreadCountRole:   return item.unreadCount;
    case GroupRole:         return item.group;
    case UnreadRole:        return item.unread || item.unreadCount > 0;
    default:                return {};
    }
}

QHash<int, QByteArray> SessionListModel::roleNames() const
{
    auto roles = QAbstractListModel::roleNames();
    roles[IdRole] = "sessionId";
    roles[NameRole] = "name";
    roles[TimeRole] = "time";
    roles[PreviewRole] = "preview";
    roles[AvatarTextRole] = "avatarText";
    roles[AvatarStartRole] = "avatarStart";
    roles[AvatarEndRole] = "avatarEnd";
    roles[UnreadCountRole] = "unreadCount";
    roles[GroupRole] = "group";
    roles[UnreadRole] = "unread";
    return roles;
}

void SessionListModel::setSessions(const QVector<SessionItem> &sessions)
{
    beginResetModel();
    m_sessions = sessions;
    endResetModel();
}

void SessionListModel::upsertSession(const SessionItem &session)
{
    for (int row = 0; row < m_sessions.size(); ++row)
    {
        if (m_sessions[row].id == session.id)
        {
            m_sessions[row] = session;
            const QModelIndex changed = index(row, 0);
            emit dataChanged(changed, changed);
            return;
        }
    }

    const int newRow = m_sessions.size();
    beginInsertRows(QModelIndex(), newRow, newRow);
    m_sessions.push_back(session);
    endInsertRows();
}

const SessionItem *SessionListModel::sessionAt(int row) const
{
    if (row < 0 || row >= m_sessions.size())
        return nullptr;
    return &m_sessions.at(row);
}

// -------------------------- SessionFilterProxyModel -----------------------

SessionFilterProxyModel::SessionFilterProxyModel(QObject *parent)
    : QSortFilterProxyModel(parent)
{
    setDynamicSortFilter(true);
}

void SessionFilterProxyModel::setSearchText(const QString &text)
{
    const QString normalized = text.trimmed();
    if (m_searchText == normalized)
        return;
    m_searchText = normalized;
    invalidateFilter();
}

void SessionFilterProxyModel::setFilter(Filter filter)
{
    if (m_filter == filter)
        return;
    m_filter = filter;
    invalidateFilter();
}

bool SessionFilterProxyModel::filterAcceptsRow(int sourceRow, const QModelIndex &sourceParent) const
{
    const QModelIndex itemIndex = sourceModel()->index(sourceRow, 0, sourceParent);
    const QString name = sourceModel()->data(itemIndex, SessionListModel::NameRole).toString();
    const QString preview = sourceModel()->data(itemIndex, SessionListModel::PreviewRole).toString();
    const bool unread = sourceModel()->data(itemIndex, SessionListModel::UnreadRole).toBool();
    const bool group = sourceModel()->data(itemIndex, SessionListModel::GroupRole).toBool();

    // 搜索同时匹配名称和最后一条消息摘要，不区分大小写。
    if (!m_searchText.isEmpty()
        && !name.contains(m_searchText, Qt::CaseInsensitive)
        && !preview.contains(m_searchText, Qt::CaseInsensitive))
        return false;

    if (m_filter == Filter::Unread && !unread)
        return false;
    if (m_filter == Filter::Groups && !group)
        return false;
    return true;
}

// ----------------------------- SessionItemDelegate -----------------------

SessionItemDelegate::SessionItemDelegate(QObject *parent)
    : QStyledItemDelegate(parent)
{
}

QSize SessionItemDelegate::sizeHint(const QStyleOptionViewItem &, const QModelIndex &) const
{
    // 与 HTML .convo 的 72px 最小高度相匹配。
    return QSize(220, 72);
}

void SessionItemDelegate::paint(QPainter *painter,
                                 const QStyleOptionViewItem &option,
                                 const QModelIndex &index) const
{
    painter->save();
    painter->setRenderHint(QPainter::Antialiasing, true);
    painter->setRenderHint(QPainter::TextAntialiasing, true);

    const QRect itemRect = option.rect.adjusted(1, 1, -1, -1);
    const bool selected = option.state.testFlag(QStyle::State_Selected);
    const bool hovered = option.state.testFlag(QStyle::State_MouseOver);

    // 选中项使用 HTML 的 --convo-active (#F0EBFF)，并画柔和阴影。
    if (selected)
    {
        painter->setPen(Qt::NoPen);
        painter->setBrush(Theme::ShadowConversation);
        painter->drawRoundedRect(itemRect.adjusted(0, 3, 0, 1), 15, 15);
        painter->setBrush(Theme::ConversationActive);
        painter->drawRoundedRect(itemRect, 15, 15);
    }
    else if (hovered)
    {
        painter->setPen(Qt::NoPen);
        painter->setBrush(Theme::ControlBackground);
        painter->drawRoundedRect(itemRect, 15, 15);
    }

    const int left = itemRect.left() + 9;
    const int avatarY = itemRect.top() + (itemRect.height() - 40) / 2;
    const QRect avatarRect(left, avatarY, 40, 40);

    // 绘制圆形渐变头像，暂时用名称首字代替头像图片。
    QLinearGradient avatarGradient(avatarRect.topLeft(), avatarRect.bottomRight());
    avatarGradient.setColorAt(0.0, index.data(SessionListModel::AvatarStartRole).value<QColor>());
    avatarGradient.setColorAt(1.0, index.data(SessionListModel::AvatarEndRole).value<QColor>());
    painter->setPen(Qt::NoPen);
    painter->setBrush(avatarGradient);
    painter->drawEllipse(avatarRect);

    QFont avatarFont = option.font;
    avatarFont.setPixelSize(12);
    avatarFont.setBold(true);
    painter->setFont(avatarFont);
    painter->setPen(Theme::White);
    painter->drawText(avatarRect, Qt::AlignCenter,
                      index.data(SessionListModel::AvatarTextRole).toString());

    // 右侧时间单独布局，名称文本会自动省略，避免与时间互相覆盖。
    const int textLeft = avatarRect.right() + 10;
    const int rightPadding = 9;
    const int textRight = itemRect.right() - rightPadding;
    const QString time = index.data(SessionListModel::TimeRole).toString();
    const QFontMetrics timeMetrics(option.font);
    const int timeWidth = timeMetrics.horizontalAdvance(time) + 1;
    const QRect timeRect(textRight - timeWidth, itemRect.top() + 13, timeWidth, 15);
    const QRect nameRect(textLeft, itemRect.top() + 10,
                         qMax(10, timeRect.left() - textLeft - 6), 18);

    QFont nameFont = option.font;
    nameFont.setPixelSize(11);
    nameFont.setBold(true);
    painter->setFont(nameFont);
    painter->setPen(Theme::Ink);
    const QString name = painter->fontMetrics().elidedText(
        index.data(SessionListModel::NameRole).toString(), Qt::ElideRight, nameRect.width());
    painter->drawText(nameRect, Qt::AlignVCenter | Qt::AlignLeft, name);

    QFont timeFont = option.font;
    timeFont.setPixelSize(9);
    painter->setFont(timeFont);
    painter->setPen(Theme::Muted);
    painter->drawText(timeRect, Qt::AlignVCenter | Qt::AlignRight, time);

    // 最后一条消息摘要；右下方预留位置给未读数字气泡。
    QFont previewFont = option.font;
    previewFont.setPixelSize(10);
    painter->setFont(previewFont);
    painter->setPen(Theme::Muted);
    const int unreadCount = index.data(SessionListModel::UnreadCountRole).toInt();
    const int previewRight = unreadCount > 0 ? textRight - 25 : textRight;
    const QRect previewRect(textLeft, itemRect.top() + 35,
                            qMax(10, previewRight - textLeft), 18);
    const QString preview = painter->fontMetrics().elidedText(
        index.data(SessionListModel::PreviewRole).toString(), Qt::ElideRight, previewRect.width());
    painter->drawText(previewRect, Qt::AlignVCenter | Qt::AlignLeft, preview);

    // 未读数大于 0 时显示紫色圆角气泡；99 条以上显示 99+。
    if (unreadCount > 0)
    {
        const QString unreadText = unreadCount > 99 ? QStringLiteral("99+") : QString::number(unreadCount);
        QFont badgeFont = option.font;
        badgeFont.setPixelSize(9);
        badgeFont.setBold(true);
        painter->setFont(badgeFont);
        const int badgeWidth = qMax(18, painter->fontMetrics().horizontalAdvance(unreadText) + 8);
        const QRect badgeRect(textRight - badgeWidth, itemRect.bottom() - 23, badgeWidth, 16);
        painter->setPen(Qt::NoPen);
        painter->setBrush(Theme::Purple);
        painter->drawRoundedRect(badgeRect, 8, 8);
        painter->setPen(Theme::White);
        painter->drawText(badgeRect, Qt::AlignCenter, unreadText);
    }

    painter->restore();
}

// ----------------------------- SessionListPanel --------------------------

SessionListPanel::SessionListPanel(QWidget *parent)
    : QWidget(parent),
      m_sourceModel(new SessionListModel(this)),
      m_proxyModel(new SessionFilterProxyModel(this)),
      m_delegate(new SessionItemDelegate(this))
{
    // 面板自身不锁定为 240px 固定宽度，QSplitter 可在 220-340px 间调整。
    setObjectName(QStringLiteral("sessionListPanel"));
    setMinimumWidth(220);
    setMaximumWidth(340);
    setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Expanding);
    setAttribute(Qt::WA_StyledBackground, true);

    m_proxyModel->setSourceModel(m_sourceModel);
    setupUi();
    setupConnections();

    // 没有服务端数据时保持空列表，不显示演示会话。
}

void SessionListPanel::setupUi()
{
    auto *rootLayout = new QVBoxLayout(this);
    rootLayout->setContentsMargins(0, 0, 0, 0);
    rootLayout->setSpacing(0);

    // 顶部标题和新建按钮。
    auto *header = new QWidget(this);
    header->setObjectName(QStringLiteral("sessionHeader"));
    auto *headerLayout = new QHBoxLayout(header);
    headerLayout->setContentsMargins(15, 22, 15, 12);
    headerLayout->setSpacing(8);

    auto *title = new QLabel(QStringLiteral("消息"), header);
    title->setObjectName(QStringLiteral("sessionTitle"));
    auto *count = new QLabel(QStringLiteral("会话"), header);
    count->setObjectName(QStringLiteral("sessionCount"));
    auto *titleStack = new QVBoxLayout;
    titleStack->setContentsMargins(0, 0, 0, 0);
    titleStack->setSpacing(2);
    titleStack->addWidget(title);
    titleStack->addWidget(count);

    auto *newButton = new QPushButton(QStringLiteral("+"), header);
    newButton->setObjectName(QStringLiteral("newSessionButton"));
    newButton->setToolTip(QStringLiteral("新建会话"));
    newButton->setFixedSize(29, 29);
    newButton->setCursor(Qt::PointingHandCursor);

    headerLayout->addLayout(titleStack);
    headerLayout->addStretch(1);
    headerLayout->addWidget(newButton, 0, Qt::AlignTop);
    rootLayout->addWidget(header);

    // 搜索框独立一行，使用 QLineEdit，后续可直接连接搜索逻辑。
    auto *searchContainer = new QWidget(this);
    searchContainer->setObjectName(QStringLiteral("sessionSearchContainer"));
    auto *searchLayout = new QHBoxLayout(searchContainer);
    searchLayout->setContentsMargins(15, 0, 15, 12);
    m_searchEdit = new QLineEdit(searchContainer);
    m_searchEdit->setObjectName(QStringLiteral("sessionSearchEdit"));
    m_searchEdit->setPlaceholderText(QStringLiteral("⌕  搜索会话或消息"));
    m_searchEdit->setClearButtonEnabled(true);
    m_searchEdit->setFixedHeight(35);
    searchLayout->addWidget(m_searchEdit);
    rootLayout->addWidget(searchContainer);

    // 筛选标签是互斥按钮，改变标签时由代理模型筛选列表内容。
    auto *filterRow = new QWidget(this);
    filterRow->setObjectName(QStringLiteral("sessionFilterRow"));
    auto *filterLayout = new QHBoxLayout(filterRow);
    filterLayout->setContentsMargins(13, 0, 13, 11);
    filterLayout->setSpacing(5);

    m_filterGroup = new QButtonGroup(this);
    m_filterGroup->setExclusive(true);
    m_allFilterButton = createFilterButton(QStringLiteral("全部"), QStringLiteral("filterAll"));
    m_unreadFilterButton = createFilterButton(QStringLiteral("未读"), QStringLiteral("filterUnread"));
    m_groupsFilterButton = createFilterButton(QStringLiteral("群聊"), QStringLiteral("filterGroups"));
    m_allFilterButton->setChecked(true);
    m_filterGroup->addButton(m_allFilterButton, static_cast<int>(SessionFilterProxyModel::Filter::All));
    m_filterGroup->addButton(m_unreadFilterButton, static_cast<int>(SessionFilterProxyModel::Filter::Unread));
    m_filterGroup->addButton(m_groupsFilterButton, static_cast<int>(SessionFilterProxyModel::Filter::Groups));
    filterLayout->addWidget(m_allFilterButton);
    filterLayout->addWidget(m_unreadFilterButton);
    filterLayout->addWidget(m_groupsFilterButton);
    filterLayout->addStretch(1);
    rootLayout->addWidget(filterRow);

    // 关键要求：列表只使用 QListView + 模型 + Delegate，不创建 QWidget 列表项。
    m_listView = new QListView(this);
    m_listView->setObjectName(QStringLiteral("sessionListView"));
    m_listView->setModel(m_proxyModel);
    m_listView->setItemDelegate(m_delegate);
    m_listView->setSelectionMode(QAbstractItemView::SingleSelection);
    m_listView->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_listView->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_listView->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    m_listView->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_listView->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    m_listView->setSpacing(2);
    m_listView->setMouseTracking(true);
    m_listView->setUniformItemSizes(true);
    rootLayout->addWidget(m_listView, 1);

    // QSS 通过 Qt 资源系统加载，部署时不依赖当前工作目录。
    QFile styleFile(QStringLiteral(":/styles/SessionListPanel.qss"));
    if (styleFile.open(QIODevice::ReadOnly | QIODevice::Text))
        setStyleSheet(QString::fromUtf8(styleFile.readAll()));

    // 把“新建会话”按钮连接到外部信号，面板本身不擅自实现创建业务。
    connect(newButton, &QPushButton::clicked, this, &SessionListPanel::newSessionRequested);
}

void SessionListPanel::setupConnections()
{
    connect(m_searchEdit, &QLineEdit::textChanged, this, [this](const QString &text)
    {
        m_proxyModel->setSearchText(text);
        restoreActiveSelectionIfVisible();
    });

    connect(m_filterGroup, QOverload<int>::of(&QButtonGroup::idClicked), this,
            [this](int id) {
                setActiveFilter(static_cast<SessionFilterProxyModel::Filter>(id));
            });

    connect(m_listView, &QListView::clicked, this, [this](const QModelIndex &proxyIndex) {
        const QString id = proxyIndex.data(SessionListModel::IdRole).toString();
        if (id.isEmpty() || id == m_selectedSessionId)
        {
            return;
        }
        m_selectedSessionId = id;
        emit sessionActivated(id);
    });
}

QPushButton *SessionListPanel::createFilterButton(const QString &text, const QString &objectName)
{
    auto *button = new QPushButton(text, this);
    button->setObjectName(objectName);
    button->setCheckable(true);
    button->setCursor(Qt::PointingHandCursor);
    button->setFixedHeight(28);
    return button;
}

void SessionListPanel::setActiveFilter(SessionFilterProxyModel::Filter filter)
{
    m_proxyModel->setFilter(filter);
    // 筛选只影响左侧可见条目，不应隐式切换右侧正在聊天的会话。
    restoreActiveSelectionIfVisible();
}

void SessionListPanel::restoreActiveSelectionIfVisible()
{
    if (m_selectedSessionId.isEmpty())
    {
        m_listView->setCurrentIndex(QModelIndex());
        return;
    }

    QModelIndex sourceIndex;
    for (int row = 0; row < m_sourceModel->rowCount(); ++row)
    {
        const SessionItem *item = m_sourceModel->sessionAt(row);
        if (item != nullptr && item->id == m_selectedSessionId)
        {
            sourceIndex = m_sourceModel->index(row, 0);
            break;
        }
    }

    const QModelIndex proxyIndex = sourceIndex.isValid()
        ? m_proxyModel->mapFromSource(sourceIndex) : QModelIndex();
    // 当前会话在筛选结果中就恢复选中；被筛选隐藏时清掉视觉选中，但保留真实会话 ID。
    m_listView->setCurrentIndex(proxyIndex);
}

void SessionListPanel::setSessions(const QVector<SessionItem> &sessions,
                                   quint64 preferredConversationId)
{
    const QString oldSelectedId = m_selectedSessionId;
    const int oldScrollValue = m_listView->verticalScrollBar()->value();
    m_sourceModel->setSessions(sessions);

    if (sessions.isEmpty())
    {
        m_selectedSessionId.clear();
        m_listView->setCurrentIndex(QModelIndex());
        // 切换账号后不保留上一个账号的搜索/筛选条件，避免真实会话被旧条件全部隐藏。
        m_searchEdit->clear();
        m_allFilterButton->setChecked(true);
        m_proxyModel->setFilter(SessionFilterProxyModel::Filter::All);
        return;
    }

    // 只有调用方明确指定新会话时才强制显示它，并允许清除当前搜索/筛选条件。
    if (preferredConversationId > 0)
    {
        bool exists = false;
        for (int row = 0; row < m_sourceModel->rowCount(); ++row)
        {
            const SessionItem *item = m_sourceModel->sessionAt(row);
            if (item != nullptr && item->id == QString::number(preferredConversationId))
            {
                exists = true;
                break;
            }
        }
        if (exists && selectConversationById(preferredConversationId))
        {
            return;
        }
    }

    // 服务端会话摘要刷新会重置模型；先尽量保留原活动会话，但不强行重置搜索/筛选。
    if (!oldSelectedId.isEmpty())
    {
        bool stillExists = false;
        QModelIndex sourceIndex;
        for (int row = 0; row < m_sourceModel->rowCount(); ++row)
        {
            const SessionItem *item = m_sourceModel->sessionAt(row);
            if (item != nullptr && item->id == oldSelectedId)
            {
                stillExists = true;
                sourceIndex = m_sourceModel->index(row, 0);
                break;
            }
        }
        if (stillExists)
        {
            m_selectedSessionId = oldSelectedId;
            m_listView->setCurrentIndex(m_proxyModel->mapFromSource(sourceIndex));
            // 如果活动会话被当前搜索/筛选隐藏，mapFromSource 返回无效索引；仍保留活动会话 ID。
            QTimer::singleShot(0, this, [this, oldScrollValue]()
            {
                m_listView->verticalScrollBar()->setValue(oldScrollValue);
            });
            return;
        }
    }

    // 首次收到会话或原会话已不存在时，才选择首条可见会话并发出切换信号。
    if (m_proxyModel->rowCount() > 0)
    {
        const QModelIndex first = m_proxyModel->index(0, 0);
        const QString firstId = first.data(SessionListModel::IdRole).toString();
        m_listView->setCurrentIndex(first);
        if (m_selectedSessionId != firstId)
        {
            m_selectedSessionId = firstId;
            emit sessionActivated(firstId);
        }
    }
    else
    {
        m_selectedSessionId.clear();
        m_listView->setCurrentIndex(QModelIndex());
    }

    QTimer::singleShot(0, this, [this, oldScrollValue]()
    {
        m_listView->verticalScrollBar()->setValue(oldScrollValue);
    });
}

bool SessionListPanel::selectConversationById(quint64 conversationId)
{
    if (conversationId == 0)
    {
        return false;
    }

    const QString wantedId = QString::number(conversationId);
    QModelIndex sourceIndex;
    for (int row = 0; row < m_sourceModel->rowCount(); ++row)
    {
        const SessionItem *item = m_sourceModel->sessionAt(row);
        if (item != nullptr && item->id == wantedId)
        {
            sourceIndex = m_sourceModel->index(row, 0);
            break;
        }
    }
    if (!sourceIndex.isValid())
    {
        return false;
    }

    QModelIndex proxyIndex = m_proxyModel->mapFromSource(sourceIndex);
    if (!proxyIndex.isValid())
    {
        // 新建会话后，即使搜索/筛选条件挡住了它，也要让用户看到刚打开的会话。
        m_searchEdit->clear();
        m_allFilterButton->setChecked(true);
        m_proxyModel->setFilter(SessionFilterProxyModel::Filter::All);
        proxyIndex = m_proxyModel->mapFromSource(sourceIndex);
    }
    if (!proxyIndex.isValid())
    {
        return false;
    }

    m_listView->setCurrentIndex(proxyIndex);
    m_listView->scrollTo(proxyIndex);
    if (m_selectedSessionId != wantedId)
    {
        m_selectedSessionId = wantedId;
        emit sessionActivated(wantedId);
    }
    return true;
}

SessionListModel *SessionListPanel::sessionModel() const
{
    return m_sourceModel;
}

QListView *SessionListPanel::sessionView() const
{
    return m_listView;
}
