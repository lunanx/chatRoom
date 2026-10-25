#pragma once

#include <QAbstractListModel>
#include <QColor>
#include <QSortFilterProxyModel>
#include <QStyledItemDelegate>
#include <QWidget>
#include <QVector>
#include <QtGlobal>

class QButtonGroup;
class QLineEdit;
class QListView;
class QPushButton;

// 一条会话在列表中展示所需的数据。
// 会话数据来自服务端响应；模型本身不生成演示记录。
struct SessionItem
{
    QString id;                 // 会话唯一标识
    QString name;               // 联系人或群聊名称
    QString time;               // 最近一条消息的时间
    QString preview;            // 最近一条消息的摘要
    QString avatarText;         // 暂无头像图片时显示的文字
    QColor avatarStart;         // 头像渐变起始色
    QColor avatarEnd;           // 头像渐变结束色
    int unreadCount = 0;        // 未读消息数；0 表示不显示未读气泡
    bool group = false;          // 是否为群聊
    bool unread = false;         // 是否属于“未读”筛选项
    quint64 conversationId = 0;  // 数据库 Conversation.id；0 表示当前没有选中真实会话。
};

// QListView 使用的数据模型：只负责保存会话数据并向视图提供角色数据。
class SessionListModel : public QAbstractListModel
{
    Q_OBJECT

public:
    enum Role
    {
        IdRole = Qt::UserRole + 1,
        NameRole,
        TimeRole,
        PreviewRole,
        AvatarTextRole,
        AvatarStartRole,
        AvatarEndRole,
        UnreadCountRole,
        GroupRole,
        UnreadRole
    };

    explicit SessionListModel(QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    QHash<int, QByteArray> roleNames() const override;

    // 用一组新数据替换当前会话列表，适合后续接收服务器会话列表响应时调用。
    void setSessions(const QVector<SessionItem> &sessions);
    // 添加单条会话；如果 id 已存在则更新原记录。
    void upsertSession(const SessionItem &session);
    const SessionItem *sessionAt(int row) const;

private:
    QVector<SessionItem> m_sessions;
};

// 过滤模型把搜索框和筛选标签的条件应用到源模型，不复制会话数据。
class SessionFilterProxyModel : public QSortFilterProxyModel
{
    Q_OBJECT

public:
    enum class Filter
    {
        All,
        Unread,
        Groups
    };

    explicit SessionFilterProxyModel(QObject *parent = nullptr);
    void setSearchText(const QString &text);
    void setFilter(Filter filter);

protected:
    bool filterAcceptsRow(int sourceRow, const QModelIndex &sourceParent) const override;

private:
    QString m_searchText;
    Filter m_filter = Filter::All;
};

// 自绘会话条目：头像、名称、时间、消息摘要和未读气泡都由 QPainter 绘制。
// Qt 的 item delegate 使用 paint() 而不是 QWidget 的 paintEvent()。
class SessionItemDelegate : public QStyledItemDelegate
{
    Q_OBJECT

public:
    explicit SessionItemDelegate(QObject *parent = nullptr);
    void paint(QPainter *painter,
               const QStyleOptionViewItem &option,
               const QModelIndex &index) const override;
    QSize sizeHint(const QStyleOptionViewItem &option,
                   const QModelIndex &index) const override;
};

// 中间会话栏，对应 HTML 中的 .conversation-rail。
// 宽度使用 Preferred 策略并设置最小/最大值，方便将来放进 QSplitter 中拖拽调整。
class SessionListPanel : public QWidget
{
    Q_OBJECT

public:
    explicit SessionListPanel(QWidget *parent = nullptr);

    // 给界面提供更新会话数据的入口；模型会自动通知 QListView 重绘。
    void setSessions(const QVector<SessionItem> &sessions, quint64 preferredConversationId = 0);
    // 选中指定数据库会话；当当前筛选条件将它隐藏时，会临时恢复“全部”视图。
    bool selectConversationById(quint64 conversationId);
    SessionListModel *sessionModel() const;
    QListView *sessionView() const;

signals:
    void newSessionRequested();
    void sessionActivated(const QString &sessionId);

private:
    void setupUi();
    void setupConnections();
    QPushButton *createFilterButton(const QString &text, const QString &objectName);
    void setActiveFilter(SessionFilterProxyModel::Filter filter);

    QLineEdit *m_searchEdit = nullptr;
    QListView *m_listView = nullptr;
    QPushButton *m_allFilterButton = nullptr;
    QPushButton *m_unreadFilterButton = nullptr;
    QPushButton *m_groupsFilterButton = nullptr;
    QButtonGroup *m_filterGroup = nullptr;
    SessionListModel *m_sourceModel = nullptr;
    SessionFilterProxyModel *m_proxyModel = nullptr;
    SessionItemDelegate *m_delegate = nullptr;
    QString m_selectedSessionId;
};
