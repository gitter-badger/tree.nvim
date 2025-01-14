#include <QFileInfo>
#include <QDebug>
#include <cinttypes>
#include <cwchar>
#include "msgpackrequest.h"
#include "tree.h"

#if defined(Q_OS_WIN)
extern int mk_wcwidth(wchar_t ucs);
#endif
using std::string;
namespace NeovimQt {

QString Tree::clipboard;
NeovimConnector * Tree::m_nvim;
const int kStop = 40;
Tree::Tree(){
}

Tree::Tree(int bufnr, int ns_id)
    : bufnr(bufnr), icon_ns_id(ns_id)
{

}

int countgrid(const QString &s)
{
    int n = s.count();
    int ans = 0;
    for (int i = 0; i < n; i++) {
        const QChar c = s.at(i);
        ushort uNum = c.unicode();
        // Characters can be full-width or half-width (with invisible characters being 0-width).
        // Ref:
        //  https://bugreports.qt.io/browse/QTBUG-9956
        //  https://www.php.net/manual/en/function.mb-strwidth.php
        //  https://www.ibm.com/support/knowledgecenter/beta/fi/ssw_ibm_i_74/rtref/wcwidth.htm
        //  https://embedeo.org/ws/doc/man_windows/
        wchar_t wc = uNum;
        // qDebug() << QString(c) << wcwidth(wta);
#if defined(Q_OS_WIN)
        if (mk_wcwidth(wc)==2) {
            ans++;
        }
#else
        if (wcwidth(wc)==2) {
            ans++;
        }
#endif
    }
    return n + ans;  // screen cells
}

void Tree::makeline(const int pos, QByteArray &line)
{
    assert(0<=pos&&pos<col_map["filename"].size());
    // mark:indent:git:icons:filename:type
    int start = 0;
    foreach (const QString &col, cfg.columns) {
        const Cell & cell = col_map[col][pos];
        QString spc(cell.col_start-start, ' ');
        line.append(std::move(spc));
        line.append(cell.text);
        QString cell_str(cell.text);
        QString spc_after(cell.col_end-countgrid(cell_str)-cell.col_start, ' ');
        line.append(std::move(spc_after));
        start = cell.col_end;
    }
    // qDebug()<<"mkline"<<pos<<":"<<line;
}
void Tree::changeRoot(QString path)
{
    // TODO: cursor history
    QDir dir(path);
    if (!dir.exists()) {
        return;
    }
    const QString & rootPath = dir.absolutePath();
    expandStore.insert(rootPath, true);

    // QDir::NoSymLinks

    // QFileInfo root_fi(path);
    FileItem::update_gmap(path);

    erase_entrylist(0, m_fileitem.size());

    // FIXME: root and its child's level inconsistent
    FileItem *fileitem = new FileItem;
    fileitem->level = -1;
    fileitem->opened_tree = true;
    fileitem->fi = QFileInfo(dir.absolutePath());
    m_fileitem.append(fileitem);

    insert_rootcell(0);
    // FIXME: when icon not available
    col_map["icon"][0].text = "";

    QList<QByteArray> ret;
    QByteArray line;
    makeline(0, line);
    ret.append(line);

    QList<FileItem*> child_fileitem;
    entryInfoListRecursively(*m_fileitem[0], child_fileitem);
    for (int i=0;i<child_fileitem.size();++i)
        m_fileitem.insert(1+i, child_fileitem[i]);

    insert_entrylist(child_fileitem, 1, ret);

    buf_set_lines(0, -1, true, std::move(ret));

    hline(0, m_fileitem.size());
}

/// Insert columns
void Tree::insert_item(const int pos)
{
    const FileItem &fileitem = *m_fileitem[pos];
    int start = 0;
    int byte_start = 0;
    foreach (const QString &col, cfg.columns) {
        Cell cell(cfg, fileitem, col);
        QString cell_str(cell.text);
        cell.byte_start = byte_start;
        cell.byte_end = byte_start+cell.text.size();
        cell.col_start = start;
        if (col=="filename") {
            cell.col_end = start + countgrid(cell_str);
        }
        else {
            cell.col_end = start + cell_str.size();
        }

        // NOTE: alignment
        if (col=="filename") {
            int tmp = kStop - cell.col_end;
            // TODO:此处都设置成统一列，在makeline时进行截断
            if (tmp >0) {
                cell.col_end+=tmp;
                cell.byte_end+=tmp;
            }
        }

        int sep = (col=="indent"?0:1);
        start = cell.col_end + sep;
        byte_start = cell.byte_end + sep;
        if (!col_map.contains(col)) {
            col_map.insert(col, QList<Cell>());
        }
        // TODO: 考虑编译器优化
        col_map[col].insert(pos, std::move(cell));
        // qDebug() << col;
    }
}

void Tree::insert_rootcell(const int pos)
{
    const FileItem &fileitem = *m_fileitem[pos];
    int start = 0;
    int byte_start = 0;
    foreach (const QString &col, cfg.columns) {
        Cell cell(cfg, fileitem, col);
        cell.col_start = start;
        if (col=="filename") {
            QByteArray filename(fileitem.fi.absoluteFilePath().toUtf8());
            if (!fileitem.fi.isRoot() && fileitem.fi.isDir()) {
                filename.append("/");
            }
            filename.prepend(cfg.root_marker.c_str());
            cell.text = filename;
        }
        QString cell_str(cell.text);
        cell.byte_start = byte_start;
        cell.byte_end = byte_start+cell.text.size();
        cell.col_end = start + countgrid(cell_str);

        int sep = (col=="indent"?0:1);
        start = cell.col_end + sep;
        byte_start = cell.byte_end + sep;

        if (!col_map.contains(col)) {
            col_map.insert(col, QList<Cell>());
        }
        // TODO: 考虑编译器优化
        col_map[col].insert(pos, std::move(cell));
    }
}
/// pos is 0-based row number.
void Tree::insert_entrylist(const QList<FileItem*>& fil, const int pos, QList<QByteArray>& ret)
{
    int file_count = fil.count();
    for (int i = 0; i < file_count; ++i) {
        // QString absolute_file_path = file_info.absoluteFilePath();
        // QByteArray absolute_file_path(file_info.absoluteFilePath().toUtf8());

        insert_item(pos+i);

        QByteArray line;
        makeline(pos+i, line);
        ret.append(std::move(line));
        // if (QString::compare(suffix, QString("png"), Qt::CaseInsensitive) == 0) { }
    }
}
// TODO: 通过FileItem的parent查询
/// l is 0-based row number.
/// NOTE: root.level=-1
int Tree::find_parent(int l)
{
    int level = m_fileitem.at(l)->level;
    for (int i=l-1;i>=0;--i)
    {
        const FileItem &fn = *m_fileitem.at(i);
        if (fn.level == level-1)
            return i;
    }
    return -1;
}

/// [l, l] <=> no sub files; l is parent row number(0-based).
std::tuple<int, int> Tree::find_range(int l)
{
    int s=l, i;
    int level = m_fileitem.at(l)->level;
    for (i=l+1;i<m_fileitem.size();++i)
    {
        int l = m_fileitem.at(i)->level;
        if (level >= l)
            break;
    }
    return std::make_tuple(s, i-1);
}

/// 0-based [sl, el).
void Tree::hline(int sl, int el)
{
    NeovimQt::NeovimApi6 *b = m_nvim->api6();
    int bufnr = this->bufnr;
    b->nvim_buf_clear_namespace(bufnr, icon_ns_id, sl, el);
    for (int i = sl;i<el;++i)
    {
        const FileItem &fileitem = *m_fileitem[i];
        char name[40];
        char cmd[80];

        foreach (const QString &col, cfg.columns) {
            const Cell & cell = col_map[col][i];

            if(col=="filename") {
                sprintf(name, "tree_%s_%d", col.toStdString().c_str(), fileitem.fi.isDir());
                sprintf(cmd, "hi %s guifg=%s", name, cell.color.toStdString().c_str());
                b->nvim_command(cmd);
                b->nvim_buf_add_highlight(bufnr, icon_ns_id, name, i, cell.byte_start, cell.byte_start+cell.text.size());
            } else if(col=="icon") {
                // :hi tree_<tab>
                sprintf(name, "tree_%s", cell.text.data());
                sprintf(cmd, "silent hi %s guifg=%s", name, cell.color.toStdString().c_str());
                b->nvim_command(cmd);
                // qDebug() << icon.col_start<< "->"<<icon.col_end;
                // auto req_hl = b->nvim_buf_add_highlight(bufnr, 0, "String", 0, 0, 3);
                b->nvim_buf_add_highlight(bufnr, icon_ns_id, name, i, cell.byte_start, cell.byte_start+cell.text.size());
                // qDebug() << icon.text;
            } else if (col=="size"){
                sprintf(name, "tree_%s", col.toStdString().c_str());
                sprintf(cmd, "hi %s guifg=%s", name, cell.color.toStdString().c_str());
                b->nvim_command(cmd);
                b->nvim_buf_add_highlight(bufnr, icon_ns_id, name, i, cell.byte_start, cell.byte_start+cell.text.size());
            }
        }
    }
}

/// 0-based [sl, el).
void Tree::redraw_line(int sl, int el)
{
    char format[] = "%s (1-based): [%d, %d]";
    qDebug(format, __PRETTY_FUNCTION__, sl+1, el);

    QList<QByteArray> ret;
    for (int i = sl; i < el; ++i) {
        FileItem & fileitem = *m_fileitem[i];

        int start = 0;
        int byte_start = 0;
        foreach (const QString &col, cfg.columns) {
            Cell& cell = col_map[col][i];
            if (col=="mark"){
            }else if(col=="indent"){
            }else if(col=="git"){
                // TODO: Git::update_gmap(fn);
                cell.update_git(fileitem.fi);
            }else if(col=="icon"){
                cell.update_icon(fileitem);
            } else if(col=="filename"){
            }else if(col=="size"){
                cell.update_size(fileitem.fi);
            }
            QString cell_str(cell.text);
            cell.col_start =start;
            cell.col_end = start + countgrid(cell_str);
            cell.byte_start = byte_start;
            cell.byte_end = byte_start+cell.text.size();

            // qDebug() << col;

            if (col=="filename")
            {
                int tmp = kStop - cell.col_end;
                if (tmp >0)
                {
                    cell.col_end+=tmp;
                    cell.byte_end+=tmp;
                }
            }
            int sep = (col=="indent"?0:1);
            start = cell.col_end + sep;
            byte_start = cell.byte_end + sep;
        }

        QByteArray line;
        makeline(i, line);
        ret.append(std::move(line));
    }
    buf_set_lines(sl, el, true, std::move(ret));
    hline(sl, el);
}

/// redraw parent and children
/// assert l is 0-based parent index
void Tree::redraw_recursively(int l)
{
    assert(0 <= l && l < m_fileitem.size());
    FileItem &cur = *m_fileitem[l];

    std::tuple<int, int> se = find_range(l);
    int s = std::get<0>(se) + 1;
    int e = std::get<1>(se) + 1;
    qDebug("%s redraw range(1-based): [%d, %d]", __PRETTY_FUNCTION__, s+1, e);

    erase_entrylist(s, e);

    QList<FileItem*> child_fileitem;
    // const QString &p = cur.fi.absoluteFilePath();
    entryInfoListRecursively(cur, child_fileitem);
    for (int i = 0; i < child_fileitem.size(); ++i)
        m_fileitem.insert(l + 1 + i, child_fileitem[i]);

    int file_count = child_fileitem.count();
    if (file_count <= 0) {
        return;
    }

    QList<QByteArray> ret;
    insert_entrylist(child_fileitem, l + 1, ret);
    buf_set_lines(s, e, true, ret);
    hline(l + 1, l + 1 + ret.size());

    return;
}
/// erase [s, e)
void Tree::erase_entrylist(const int s, const int e)
{
    foreach (const QString &col, cfg.columns) {
        QList<Cell> & cell = col_map[col];
        cell.erase(cell.begin()+s, cell.begin()+e);
    }
    for (int i = s; i < e; ++i) {
        delete m_fileitem.at(i);
    }
    m_fileitem.erase(m_fileitem.begin()+s, m_fileitem.begin()+e);
}

/// l is 0-based row number.
void Tree::open_tree(int l)
{
    qDebug() << __PRETTY_FUNCTION__;
    assert(0 <= l && l < m_fileitem.size());
    // if (l == 0) return;
    QList<QByteArray> ret;
    FileItem &cur = *m_fileitem[l];

    NeovimQt::NeovimApi6 *b = m_nvim->api6();

    if (cur.fi.isDir() && !cur.opened_tree) {
        cur.opened_tree = true;
        const QString & rootPath = cur.fi.absoluteFilePath();
        expandStore.insert(rootPath, true);
        redraw_line(l, l + 1);
        QList<FileItem*> child_fileitem;
        entryInfoListRecursively(cur, child_fileitem);
        for (int i=0;i<child_fileitem.size();++i)
            m_fileitem.insert(l+1+i, child_fileitem[i]);

        int file_count = child_fileitem.count();
        if (file_count <= 0) {
            return;
        }

        insert_entrylist(child_fileitem, l + 1, ret);
        buf_set_lines(l+1, l+1, true, ret);
        hline(l + 1, l + 1 + ret.size());
        return;
    }
    else if (cur.opened_tree) {
        const QString & p = cur.fi.absoluteFilePath();
        if (expandStore.contains(p) && expandStore[p]) {
            expandStore[p] = false;
        }
        std::tuple<int, int> se = find_range(l);
        int s = std::get<0>(se) + 1;
        int e = std::get<1>(se) + 1;
        qDebug("\tclose range(1-based): [%d, %d]", s+1, e);
        buf_set_lines(s, e, true, {});

        erase_entrylist(s, e);
        cur.opened_tree = false;
        redraw_line(l, l + 1);
        return;
    }
    else if (find_parent(l) >= 0) {
        int parent = find_parent(l);
        std::tuple<int, int> se = find_range(parent);
        int s = std::get<0>(se) + 1;
        int e = std::get<1>(se) + 1;

        qDebug("\tclose range(1-based): [%d, %d]", s+1, e);

        buf_set_lines(s, e, true, {});
        // ref to https://github.com/equalsraf/neovim-qt/issues/596
        b->nvim_win_set_cursor(0, QPoint{0, s});
        erase_entrylist(s, e);

        FileItem &father = *m_fileitem[parent];
        father.opened_tree = false;
        const QString & p = father.fi.absoluteFilePath();
        if (expandStore.contains(p) && expandStore[p]) {
            expandStore[p] = false;
        }
        redraw_line(parent, parent + 1);
        return;
    }

    return;
}

void Tree::open(const int l, const QList<QVariant> &args)
{
    const QFileInfo &fi = m_fileitem[l]->fi;
    NeovimQt::NeovimApi6 *b = m_nvim->api6();
    if (fi.isDir()) {
        changeRoot(fi.absoluteFilePath());
    }
    else if (args.size()>0 && args[0]=="vsplit") {
        QString path = fi.absoluteFilePath();
        qDebug() << "vsplit" << path;
        b->vim_call_function("tree#util#execute_path", {"rightbelow vsplit", path});
    }
    else {
        QString path = fi.absoluteFilePath();
        qDebug() << "drop" << path;
        b->vim_call_function("tree#util#execute_path", {"drop", path});
    }
}


void Tree::open_or_close_tree_recursively(int l)
{
    qDebug() << __PRETTY_FUNCTION__;
    assert(0 <= l && l < m_fileitem.size());
    if (l == 0) return;
    QList<QByteArray> ret;
    FileItem &cur = *m_fileitem[l];

    NeovimQt::NeovimApi6 *b = m_nvim->api6();

    if (cur.fi.isDir() && !cur.opened_tree) {
        cur.opened_tree = true;
        const QString &rootPath = cur.fi.absoluteFilePath();
        expandStore.insert(rootPath, true);
        redraw_line(l, l + 1);
        QList<FileItem*> child_fileitem;
        expandRecursively(cur, child_fileitem);
        for (int i = 0; i < child_fileitem.size(); ++i)
            m_fileitem.insert(l + 1 + i, child_fileitem[i]);

        int file_count = child_fileitem.count();
        if (file_count <= 0) {
            return;
        }

        insert_entrylist(child_fileitem, l + 1, ret);
        buf_set_lines(l+1, l+1, true, ret);
        hline(l + 1, l + 1 + ret.size());
        return;
    }
    else if (cur.opened_tree) {
        const QString &p = cur.fi.absoluteFilePath();
        if (expandStore.contains(p) && expandStore[p]) {
            expandStore[p] = false;
        }
        std::tuple<int, int> se = find_range(l);
        int s = std::get<0>(se) + 1;
        int e = std::get<1>(se) + 1;
        qDebug("\tclose range(1-based): [%d, %d]", s+1, e);
        buf_set_lines(s, e, true, {});
        shrinkRecursively(p);
        erase_entrylist(s, e);
        cur.opened_tree = false;
        redraw_line(l, l + 1);
        return;
    }
    else if (find_parent(l) >= 0) {
        int parent = find_parent(l);
        std::tuple<int, int> se = find_range(parent);
        int s = std::get<0>(se) + 1;
        int e = std::get<1>(se) + 1;

        qDebug("\tclose range(1-based): [%d, %d]", s+1, e);

        buf_set_lines(s, e, true, {});
        // ref to https://github.com/equalsraf/neovim-qt/issues/596
        b->nvim_win_set_cursor(0, QPoint{0, s});

        FileItem &father = *m_fileitem[parent];
        father.opened_tree = false;
        const QString &p = father.fi.absoluteFilePath();
        if (expandStore.contains(p) && expandStore[p]) {
            expandStore[p] = false;
        }
        shrinkRecursively(p);
        erase_entrylist(s, e);
        redraw_line(parent, parent + 1);
        return;
    }

    return;
}

// get entryInfoList recursively
void Tree::entryInfoListRecursively(const FileItem& item,
                                   QList<FileItem*> &fileitem_lst)
{
    QDir dir(item.fi.absoluteFilePath());
    set_dir(dir);
    const int level = item.level+1;
    QFileInfoList child = dir.entryInfoList();
    int file_count = child.count();
    if (file_count <= 0) {
        return;
    }
    for (int i = 0; i < file_count; ++i) {
        QFileInfo fi = child.at(i);

        FileItem *fileitem = new FileItem;
        fileitem->fi = fi;
        fileitem->level = level;
        fileitem->parent = &item;
        if (i==file_count-1)
            fileitem->last=true;
        QString p = fi.absoluteFilePath();

        if (expandStore.contains(p) && expandStore[p]) {
            fileitem->opened_tree = true;
            fileitem_lst.append(fileitem);
            entryInfoListRecursively(*fileitem, fileitem_lst);
        }
        else {
            fileitem_lst.append(fileitem);
        }
    }
}


// shrink recursively
void Tree::shrinkRecursively(const QString &path)
{
    QDir dir(path);
    set_dir(dir);
    QFileInfoList child = dir.entryInfoList();
    int file_count = child.count();
    if (file_count <= 0) {
        return;
    }
    for (int i = 0; i < file_count; ++i) {
        QFileInfo file_info = child.at(i);
        QString p = file_info.absoluteFilePath();

        if (expandStore.contains(p) && expandStore[p]) {
            expandStore[p] = false;
            shrinkRecursively(p);
        }
    }
    return;
}

// shrink recursively
void Tree::expandRecursively(const FileItem &item,
                            QList<FileItem*> &fileitem_lst)
{
    QDir dir(item.fi.absoluteFilePath());
    set_dir(dir);
    const int level = item.level+1;
    QFileInfoList child = dir.entryInfoList();
    int file_count = child.count();
    if (file_count <= 0) {
        return;
    }
    for (int i = 0; i < file_count; ++i) {
        QFileInfo file_info = child.at(i);

        FileItem *fileitem = new FileItem;
        fileitem->fi = file_info;
        fileitem->level = level;
        fileitem->parent = &item;
        if (i==file_count-1)
            fileitem->last=true;

        if (file_info.isDir()) {
            QString p = file_info.absoluteFilePath();
            expandStore.insert(p, true);
            fileitem->opened_tree = true;
            fileitem_lst.append(fileitem);
            expandRecursively(*fileitem, fileitem_lst);
        }
        else {
            fileitem_lst.append(fileitem);
        }
    }
    return;
}

void Tree::handleRename(const QVariant& val)
{
    NeovimQt::NeovimApi6 *b = m_nvim->api6();
    disconnect(b, &NeovimApi6::on_nvim_call_function, this, &Tree::handleRename);
    qDebug() << __PRETTY_FUNCTION__;
    input = val.toString();

    Cell & cur = col_map["filename"][ctx.cursor-1];
    FileItem & curitem = *m_fileitem[ctx.cursor-1];
    QString fn = curitem.fi.absoluteFilePath();
    QFile qf(fn);
    if (qf.rename(input)) {
        curitem.fi = QFileInfo(input);
        QByteArray text(curitem.fi.fileName().toUtf8());
        if (curitem.fi.isDir())
            text.append("/");
        cur.text = text;

        // NOTE: gmap may update
        FileItem::update_gmap(curitem.fi.absolutePath());
        redraw_line(ctx.cursor-1, ctx.cursor);
    } else{
        b->nvim_call_function("echo", {"rename error"});
    }
}

void Tree::handleNewFile(const QVariant &val)
{
    NeovimQt::NeovimApi6 *b = m_nvim->api6();
    disconnect(b, &NeovimApi6::on_nvim_call_function, this, &Tree::handleNewFile);

    input = val.toString();
    qDebug() << __PRETTY_FUNCTION__ << input;

    // Cell & cur = col_map["filename"][ctx.cursor-1];
    FileItem & fileitem = *m_fileitem[ctx.cursor-1];

    QString filename, destdir;
    if (fileitem.opened_tree) {
        filename = QDir(fileitem.fi.absoluteFilePath()).filePath(input);
        destdir = fileitem.fi.absoluteFilePath();
    }
    else{
        filename = fileitem.fi.absoluteDir().filePath(input);
        destdir = fileitem.fi.absolutePath();
    }
    QDir curdir(destdir);
    QFile file(filename);

    if (file.exists())
    {
        qDebug() << "File exists!";
        return;
    } else if(input.endsWith("/")){
        if (!curdir.mkpath(input))
            qDebug() << "Failed to create dir!";
    } else{
        file.open(QIODevice::WriteOnly);
        file.close();
    }

    if (fileitem.opened_tree) {
        redraw_recursively(ctx.cursor-1);
    } else{
        // TODO: location: find_child
        int pidx = find_parent(ctx.cursor-1);
        // NOTE: root.level=-1
        // TODO: consider named redraw_family
        redraw_recursively(pidx);
    }
}

// XXX: It override the builtin 'input()' function.
void Tree::vim_input(string prompt="", string text="", string completion="", string handle="")
{
    qDebug() << __PRETTY_FUNCTION__;
    MsgpackRequest* req;
    NeovimQt::NeovimApi6 *b = m_nvim->api6();
    if (handle=="rename")
        connect(b, &NeovimQt::NeovimApi6::on_nvim_call_function, this, &Tree::handleRename);
    else if(handle=="new_file")
        connect(b, &NeovimQt::NeovimApi6::on_nvim_call_function, this, &Tree::handleNewFile);
    QVariantList args = {prompt.c_str(), text.c_str(), completion.c_str()};
    qDebug() << args;
    req = b->nvim_call_function("input", args);
    // NOTE: MsgpackRequest::finished different with on_nvim_call_function
    // connect(req, &MsgpackRequest::finished, this, &Tree::handleInputFinish, Qt::DirectConnection);

    // if(completion!="")
    //     req = b->nvim_call_function("input", {prompt.c_str(), text.c_str(), completion.c_str()});
    // else
    //     req = b->nvim_call_function("input", {prompt.c_str(), text.c_str()});
}

void Tree::cd(const QList<QVariant>& args)
{
    NeovimQt::NeovimApi6 *b = m_nvim->api6();
    if (args.size()>0) {
        QString dir = args.at(0).toString();

        if (dir=="..") {
            QDir curdir(m_fileitem.at(0)->fi.absoluteFilePath());
            if (curdir.cd(".."))
            {
                qDebug()<< __PRETTY_FUNCTION__ << curdir;
                changeRoot(curdir.absolutePath());
            }
        }
        else {
            changeRoot(dir);
        }
    }
    else {
        changeRoot(QDir::home().absolutePath());
    }
}
void Tree::toggle_select(const int pos)
{
    // TODO: mark may not available
    Cell &cur = col_map["mark"][pos];
    FileItem& item = *m_fileitem[pos];

    item.selected = !item.selected;
    if (item.selected) {
        cur.text = mark_indicators["selected_icon"];
        targets.append(pos);
    }
    else {
        cur.text=" ";
        targets.removeOne(pos);
    }

    redraw_line(pos, pos+1);
}

// TODO: 收集无序targets
void collect_targets(const QList<FileItem> &m_fileitem)
{
    foreach (const FileItem &item, m_fileitem) {
        if (item.selected){

        }
    }
}

void Tree::action(const QString &action, const QList<QVariant> &args,
                  const QMap<QString, QVariant> context)
{
    qDebug() << __PRETTY_FUNCTION__ << action;

    NeovimQt::NeovimApi6 *b = m_nvim->api6();

    qDebug() << "action args:" << action << args;
    // qDebug() << "context:" << context;
    this->ctx = context;
    qDebug() << "cursor position(1-based): " << ctx.cursor;
    if (action == "open") {
        open(ctx.cursor - 1, args);
    }
    else if (action == "open_or_close_tree") {
        open_tree(ctx.cursor - 1);
    }
    else if (action == "cd") {
        qDebug() << args;
        cd(args);
    }
    else if (action == "rename") {
        qDebug() << action << args;
        FileItem &cur = *m_fileitem[ctx.cursor - 1];
        string info = cur.fi.absoluteFilePath().toStdString();
        // NOTE: specify handle for vim_input
        vim_input("Rename: " + info + " -> ", info, "file", "rename");
    }
    else if (action == "copy") {
        FileItem &cur = *m_fileitem[ctx.cursor - 1];
        Tree::clipboard = cur.fi.absoluteFilePath();
    }
    else if (action == "paste") {
        FileItem &cur = *m_fileitem[ctx.cursor - 1];
        QString fname = QFileInfo(clipboard).fileName();
        if (QFile::copy(clipboard, cur.fi.absoluteDir().absoluteFilePath(fname))) {
            b->nvim_call_function("tree#util#print_message", {"Copyed"});
            int pidx = find_parent(ctx.cursor-1);
            redraw_recursively(pidx);
        }
        else {
            b->nvim_call_function("tree#util#print_message", {"Failed"});
        }
    }
    else if (action == "toggle_select") {
        toggle_select(ctx.cursor - 1);
    }
    else if (action == "redraw") {
        FileItem &root = *m_fileitem[0];
        changeRoot(root.fi.absoluteFilePath());
    }
    else if (action == "new_file") {
        FileItem &cur = *m_fileitem[ctx.cursor - 1];
        string info = cur.fi.absolutePath().toStdString();
        vim_input("New File: " + info + "/", "", "file", "new_file");
    }
    else if (action == "open_tree_recursive") {
        open_or_close_tree_recursively(ctx.cursor - 1);
    }
    else if (action == "execute_system") {
        FileItem &cur = *m_fileitem[ctx.cursor - 1];
        QString info = cur.fi.absoluteFilePath();
        b->nvim_call_function("tree#util#open", {info});
    }
    else if (action == "yank_path") {
        QStringList yank;
        foreach (const int &pos, targets) {
            yank.append(m_fileitem[pos]->fi.absoluteFilePath());
        }
        if (yank.size()==0) {
            FileItem &cur = *m_fileitem[ctx.cursor - 1];
            yank << cur.fi.absoluteFilePath();
        }
        QString reg = yank.join('\n');
        b->nvim_call_function("setreg", {"\"", reg});

        yank.insert(0, "yank_path");
        QString msg = yank.join('\n');
        b->nvim_call_function("tree#util#print_message", {msg});
    }
    else if (action == "drop") {
        FileItem &cur = *m_fileitem[ctx.cursor - 1];
        QString info = cur.fi.absoluteFilePath();

        b->nvim_command("lua require('tree')");
        b->nvim_execute_lua("drop(...)", {info});
    }
    else if (action == "debug") {
        qDebug() << cfg.columns;
    }
    else if (action == "print") {
        FileItem &cur = *m_fileitem[ctx.cursor - 1];
        QString msg =cur.fi.absoluteFilePath();
        b->nvim_call_function("tree#util#print_message", {msg});
    }
    else if (action == "toggle_select_all") {
        for (int i=1;i<m_fileitem.size();++i) {
            toggle_select(i);
        }
    }
    else if (action == "toggle_ignored_files") {
        cfg.show_ignored_files=!cfg.show_ignored_files;
        FileItem &root = *m_fileitem[0];
        changeRoot(root.fi.absoluteFilePath());
    }

}

} // namespace NeovimQt
