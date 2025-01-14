#ifndef NEOVIM_QT_TREE
#define NEOVIM_QT_TREE

#include <QList>
#include <QDir>
#include <tuple>
#include "msgpackiodevice.h"
#include "column.h"
#include "neovimconnector.h"

namespace NeovimQt {

class NeovimConnector;

class Tree: public QObject
{
    Q_OBJECT
public:
    Tree();
    Tree(int bufnr, int icon_ns_id);
    static QString clipboard;
	static NeovimConnector *m_nvim;
    int bufnr = -1;
    int icon_ns_id = -1;
    Config cfg;
    Context ctx;
    QString input;
    void changeRoot(QString path);
    void open_tree(int);
    void open(const int l, const QList<QVariant> &args);
    void cd(const QList<QVariant>& args);
    void toggle_select(const int pos);
    void vim_input(string prompt, string text, string completion, string handle);
    void redraw_line(int sl, int el);
    void redraw_recursively(int l);
    void action(const QString &action, const QList<QVariant> &args, const QMap<QString, QVariant> context);
    inline void buf_set_lines(int s, int e, bool strict, QList<QByteArray> replacement)
    {
        NeovimApi6 *b = m_nvim->api6();
        b->nvim_buf_set_option(bufnr, "modifiable", true);
        b->nvim_buf_set_lines(bufnr, s, e, strict, replacement);
        b->nvim_buf_set_option(bufnr, "modifiable", false);
    };
    inline void set_dir(QDir &dir)
    {
        // TODO: QDir::setNameFilters
        QDir::Filters filters = QDir::Dirs | QDir::Files | QDir::NoDotAndDotDot;
        if (cfg.show_ignored_files)
            filters |= QDir::Hidden;
        dir.setFilter(filters);
        dir.setSorting(QDir::Name|QDir::DirsFirst);
// QDir::Name     Sort by name.
// QDir::Time     Sort by time (modification time).
// QDir::Size     Sort by file size.
// QDir::Type     Sort by file type (extension).
// QDir::Unsorted 	 Do not sort.
// QDir::NoSort     Not sorted by default.
// QDir::DirsFirst	 Put the directories first, then the files.
// QDir::DirsLast	 Put the files first, then the directories.
// QDir::Reversed	 Reverse the sort order.
// QDir::IgnoreCase Sort case-insensitively.
// QDir::LocaleAware  Sort items appropriately using the current locale settings.
    };


protected slots:
    void handleRename(const QVariant& val);
    void handleNewFile(const QVariant& val);

private:

    QList<FileItem *> m_fileitem;
    QMap<QString, QList<Cell>> col_map;
    QHash<QString, bool> expandStore;
    QList<int> targets;
    void hline(int sl, int el);
    int find_parent(int l);
    std::tuple<int, int> find_range(int l);
    void insert_entrylist(const QList<FileItem*> &, const int pos, QList<QByteArray>& ret);
    void insert_item(const int pos);
    void insert_rootcell(const int pos);
    void erase_entrylist(const int s, const int e);
    void makeline(const int pos, QByteArray &line);
    void entryInfoListRecursively(const FileItem &, QList<FileItem*>& fileitem_lst);

    void open_or_close_tree_recursively(int l);
    void shrinkRecursively(const QString &path);
    void expandRecursively(const FileItem&, QList<FileItem*> &fileitem_lst);
};

} // namespace NeovimQt

#endif
