/**********************************************************************
 *  MainWindow.cpp
 **********************************************************************
 * Copyright (C) 2017 MX Authors
 *
 * Authors: Adrian
 *          Dolphin_Oracle
 *          MX Linux <http://mxlinux.org>
 *
 * This file is part of mx-package-manager.
 *
 * mx-package-manager is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * mx-package-manager is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with mx-package-manager.  If not, see <http://www.gnu.org/licenses/>.
 **********************************************************************/


#include "mainwindow.h"
#include "ui_mainwindow.h"

#include <QFileDialog>
#include <QScrollBar>
#include <QTextStream>
#include <QtXml/QtXml>
#include <QProgressBar>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QImageReader>

#include <QDebug>

MainWindow::MainWindow(QWidget *parent) :
    QDialog(parent),
    ui(new Ui::MainWindow)
{
    ui->setupUi(this);
    setup();
}

MainWindow::~MainWindow()
{
    delete ui;
}

// Setup versious items first time program runs
void MainWindow::setup()
{
    cmd = new Cmd(this);
    arch = cmd->getOutput("arch");
    setProgressDialog();
    lock_file = new LockFile("/var/lib/dpkg/lock");
    connect(qApp, &QApplication::aboutToQuit, this, &MainWindow::cleanup);
    version = getVersion("mx-package-manager");
    this->setWindowTitle(tr("MX Package Manager"));
    ui->tabWidget->setCurrentIndex(0);
    ui->buttonCancel->setEnabled(true);
    ui->buttonInstall->setEnabled(false);
    ui->buttonUninstall->setEnabled(false);
    QStringList column_names;
    column_names << "" << "" << tr("Package") << tr("Info") << tr("Description");
    ui->treeWidget->setHeaderLabels(column_names);
    installed_packages = listInstalled();
    loadPmFiles();
    displayPopularApps();
    connect(ui->searchPopular,&QLineEdit::textChanged, this, &MainWindow::findPackage);
    ui->searchPopular->setFocus();

}

// Uninstall listed packages
void MainWindow::uninstall(const QString &names)
{
    this->hide();
    lock_file->unlock();
    cmd->run("x-terminal-emulator -e apt-get remove " + names);
    refreshPopularApps();
    this->show();
}

// Run apt-get update
void MainWindow::update()
{
    lock_file->unlock();
    setConnections();
    progress->show();
    progress->setLabelText(tr("Running apt-get update... "));
    cmd->run("apt-get update");
}


// set proc and timer connections
void MainWindow::setConnections()
{
    //connect(cmd, &Cmd::outputAvailable, this, &MainWindow::updateOutput);
    connect(cmd, &Cmd::runTime, this, &MainWindow::tock);  // processes runtime emited by Cmd to be used by a progress bar
    connect(cmd, &Cmd::started, this, &MainWindow::cmdStart);
    connect(cmd, &Cmd::finished, this, &MainWindow::cmdDone);
}

//void MainWindow::updateOutput(const QString &output)
//{
//    ui->outputBox->insertPlainText(output);
//    QScrollBar *sb = ui->outputBox->verticalScrollBar();
//    sb->setValue(sb->maximum());
//}

void MainWindow::tock(int counter, int duration) // processes tick emited by Cmd to be used by a progress bar
{
    int max_value;
    max_value = (duration != 0) ? duration : 10;
    bar->setMaximum(max_value);
    bar->setValue(counter % (max_value + 1));
}


// Load info from the .pm files
void MainWindow::loadPmFiles()
{
    QDomDocument doc;

    QStringList filter("*.pm");
    QDir dir("/usr/share/mx-package-manager-pkglist");
    QStringList pmfilelist = dir.entryList(filter);

    foreach (QString file_name, pmfilelist) {
        QFile file(dir.absolutePath() + "/" + file_name);
        if (!file.open(QFile::ReadOnly | QFile::Text)) {
            qDebug() << "Could not open: " << file.fileName();
        } else {
            if (!doc.setContent(&file)) {
                qDebug() << "Could not load document: " << file_name << "-- not valid XML?";
            } else {
                processDoc(doc);
            }
        }
        file.close();
    }
}

// Process dom documents (from .pm files)
void MainWindow::processDoc(const QDomDocument &doc)
{
    /*  Order items in list:
        0 "category"
        1 "name"
        2 "description"
        3 "installable"
        4 "screenshot"
        5 "preinstall"
        6 "install_package_names"
        7 "postinstall"
        8 "uninstall_package_names"
    */

    QString category;
    QString name;
    QString description;
    QString installable;
    QString screenshot;
    QString preinstall;
    QString postinstall;
    QString install_names;
    QString uninstall_names;
    QStringList list;

    QDomElement root = doc.firstChildElement("app");
    QDomElement element = root.firstChildElement();

    for (; !element.isNull(); element = element.nextSiblingElement()) {
        if (element.tagName() == "category") {
            category = element.text().trimmed();
        } else if (element.tagName() == "name") {
            name = element.text().trimmed();
        } else if (element.tagName() == "description") {
            description = element.text().trimmed();
        } else if (element.tagName() == "installable") {
            installable = element.text().trimmed();
        } else if (element.tagName() == "screenshot") {
            screenshot = element.text().trimmed();
        } else if (element.tagName() == "preinstall") {
            preinstall = element.text().trimmed();
        } else if (element.tagName() == "install_package_names") {
            install_names = element.text().trimmed();
            install_names.replace("\n", " ");
        } else if (element.tagName() == "postinstall") {
            postinstall = element.text().trimmed();
        } else if (element.tagName() == "uninstall_package_names") {
            uninstall_names = element.text().trimmed();
        }
    }
    // skip non-installable packages
    if ((installable == "64" && arch != "x86_64") || (installable == "32" && arch != "i686")) {
        return;
    }
    list << category << name << description << installable << screenshot << preinstall
         << postinstall << install_names << uninstall_names;
    popular_apps << list;
}

// Reloadn and refresh interface
void MainWindow::refreshPopularApps()
{
    lock_file->lock();
    ui->treeWidget->clear();
    ui->searchPopular->clear();
    ui->buttonInstall->setEnabled(false);
    ui->buttonUninstall->setEnabled(false);
    installed_packages = listInstalled();
    displayPopularApps();
}

void MainWindow::setProgressDialog()
{
    timer = new QTimer(this);
    progress = new QProgressDialog(this);
    bar = new QProgressBar(progress);
    progress->setWindowModality(Qt::WindowModal);
    progress->setWindowFlags(Qt::Dialog | Qt::CustomizeWindowHint | Qt::WindowTitleHint |Qt::WindowSystemMenuHint | Qt::WindowStaysOnTopHint);
    progress->setCancelButton(0);
    progress->setLabelText(tr("Please wait..."));
    progress->setAutoClose(false);
    progress->setBar(bar);
    bar->setTextVisible(false);
}

// Display Popular Apps in the treeWidget
void MainWindow::displayPopularApps()
{
    QTreeWidgetItem *topLevelItem = NULL;
    QTreeWidgetItem *childItem;

    foreach (QStringList list, popular_apps) {
        QString category = list[0];
        QString name = list[1];
        QString description = list[2];
        QString installable = list[3];
        QString screenshot = list[4];
        QString preinstall = list[5];
        QString postinstall = list[6];
        QString install_names = list[7];
        QString uninstall_names = list[8];

        // add package category if treeWidget doesn't already have it
        if (ui->treeWidget->findItems(category, Qt::MatchFixedString, 2).isEmpty()) {
            topLevelItem = new QTreeWidgetItem;
            topLevelItem->setText(2, category);
            ui->treeWidget->addTopLevelItem(topLevelItem);
            // topLevelItem look
            QFont font;
            font.setBold(true);
            topLevelItem->setForeground(2, QBrush(Qt::darkGreen));
            topLevelItem->setFont(2, font);
            topLevelItem->setIcon(0, QIcon::fromTheme("folder-green"));
        } else {
            topLevelItem = ui->treeWidget->findItems(category, Qt::MatchFixedString, 2).at(0); //find first match; add the child there
        }
        // add package name as childItem to treeWidget
        childItem = new QTreeWidgetItem(topLevelItem);
        childItem->setText(2, name);
        childItem->setIcon(3, QIcon::fromTheme("info"));

        // add checkboxes
        childItem->setFlags(childItem->flags() | Qt::ItemIsUserCheckable);
        childItem->setCheckState(1, Qt::Unchecked);

        // add description from file
        childItem->setText(4, description);

        // add install_names (not displayed)
        childItem->setText(5, install_names);

        // add uninstall_names (not displayed)
        childItem->setText(6, uninstall_names);

        // add screenshot url (not displayed)
        childItem->setText(7, screenshot);

        // gray out installed items
        if (checkInstalled(uninstall_names)) {
            childItem->setForeground(2, QBrush(Qt::gray));
            childItem->setForeground(4, QBrush(Qt::gray));
        }
    }
    for (int i = 0; i < 5; i++) {
        ui->treeWidget->resizeColumnToContents(i);
    }
    ui->buttonInstall->setEnabled(false);
    connect(ui->treeWidget, &QTreeWidget::itemClicked, this, &MainWindow::displayInfo);
}

// install named app, return 'false' if any steps fails
void MainWindow::installPopularApp(const QString &name)
{
    QString preinstall;
    QString postinstall;
    QString install_names;

    // get all the app info
    foreach (QStringList list, popular_apps) {
        if (list[1] == name) {
            preinstall = list[5];
            postinstall = list[6];
            install_names = list[7];
        }
    }
    setConnections();
    progress->setLabelText(tr("Pre-processing for ") + name);
    cmd->run(preinstall);

    if (install_names != "") {
        progress->hide();
        this->hide();
        setConnections();
        progress->setLabelText(tr("Installing ") + name);
        cmd->run("x-terminal-emulator -e apt-get install " + install_names);
        this->show();
        progress->show();
    }
    setConnections();
    progress->setLabelText(tr("Post-processing for ") + name);
    cmd->run(postinstall);
    progress->hide();
}

void MainWindow::installPopularApps()
{
    //this->hide();
    update();
    QTreeWidgetItemIterator it(ui->treeWidget);
    ui->treeWidget->clearSelection(); //deselect all items
    while (*it) {
        if ((*it)->checkState(1) == Qt::Checked) {
            installPopularApp((*it)->text(2));
        }
        ++it;
    }
    lock_file->lock();
    setCursor(QCursor(Qt::ArrowCursor));
    if (QMessageBox::information(this, tr("Done"),
                                 tr("Process finished.<p><b>Do you want to exit MX Package Installer?</b>"),
                                 tr("Yes"), tr("No")) == 0){
        qApp->exit(0);
    }
    refreshPopularApps();
    this->show();
}

// Check if online
bool MainWindow::checkOnline()
{
    return(system("wget -q --spider http://google.com >/dev/null 2>&1") == 0);
}

// Build the list of available packages from various source
bool MainWindow::buildPackageLists()
{
    if (!downloadPackageList()) {
        progress->hide();
        return false;
    }
    if (!readPackageList()) {
        progress->hide();
        return false;
    }



    progress->hide();
}

// Download the Packages.gz from sources
bool MainWindow::downloadPackageList()
{
    if (!checkOnline()) {
        QMessageBox::critical(this, tr("Error"), tr("Internet is not available, won't be able to download the list of packages"));
        return false;
    }
    if (tmp_dir == "") {
        tmp_dir = cmd->getOutput("mktemp -d /tmp/mxpm-XXXXXXXX");
    }
    setConnections();
    progress->setLabelText(tr("Downloading package info..."));
    progress->show();
    if (arch == "i686") {
        if (ui->radioMXtest->isChecked())  {
            return (cmd->run("wget http://mxrepo.com/mx/testrepo/dists/mx15/test/binary-i386/Packages.gz -O " + tmp_dir + "/mx15Packages.gz") == 0);
        } else {
            int err1 = cmd->run("wget ftp://ftp.us.debian.org/debian/dists/jessie-backports/main/binary-i386/Packages.gz -O " + tmp_dir + "/mainPackages.gz");
            int err2 = cmd->run("wget ftp://ftp.us.debian.org/debian/dists/jessie-backports/contrib/binary-i386/Packages.gz -O " + tmp_dir + "/contribPackages.gz");
            int err3 = cmd->run("wget ftp://ftp.us.debian.org/debian/dists/jessie-backports/non-free/binary-i386/Packages.gz -O " + tmp_dir + "/nonfreePackages.gz");
            return (err1 + err2 + err3 == 0);
        }
    } else {
        if (ui->radioMXtest->isChecked())  {
            return (cmd->run("wget http://mxrepo.com/mx/testrepo/dists/mx15/test/binary-amd64/Packages.gz -O " + tmp_dir + "/mx15Packages.gz") == 0);
        } else {
            int err1 = cmd->run("wget ftp://ftp.us.debian.org/debian/dists/jessie-backports/main/binary-amd64/Packages.gz -O " + tmp_dir + "/mainPackages.gz");
            int err2 = cmd->run("wget ftp://ftp.us.debian.org/debian/dists/jessie-backports/contrib/binary-amd64/Packages.gz -O " + tmp_dir + "/contribPackages.gz");
            int err3 = cmd->run("wget ftp://ftp.us.debian.org/debian/dists/jessie-backports/non-free/binary-amd64/Packages.gz -O " + tmp_dir + "/nonfreePackages.gz");
            return (err1 + err2 + err3 == 0);
        }
    }

}

// Process downloaded *Packages.gz files
bool MainWindow::readPackageList()
{
    progress->setLabelText(tr("Reading downloaded file..."));
    if (cmd->run("gzip -df " + tmp_dir + "/*Packages.gz") != 0) {
        return false;
    }
    QString list = cmd->getOutput(QString("IFS=$'\\n'\n"
                                          "declare -a packagename\n"
                                          "declare -a packageversion\n"
                                          "declare -a packagedescrip\n"
                                          "packagename=(`cat %0/*Packages |awk '/Package:/ && !/-Package/'|cut -d ' ' -f2`)\n"
                                          "packageversion=(`cat %0/*Packages |awk '/Version:/ && !/-Version:/'|cut -d ' ' -f2`)\n"
                                          "packagedescrip=(`cat %0/*Packages |awk '/Description:/ && !/-Description:/'|cut -d ':' -f2`)\n"
                                          "count=$(echo ${#packagename[@]})\n"
                                          "echo $count Packages\n"
                                          "i='0'\n"
                                          "while [ \"$i\" -lt \"$count\" ]; do\n"
                                              "echo \"${packagename[i]} ${packageversion[i]} ${packagedescrip[i]}\"\n"
                                              "i=$[$i+1]\n"
                                          "done").arg(tmp_dir));

    if (ui->radioMXtest->isChecked())  { // read MX Test list
        mx_list = list.split("\n");
        mx_list.removeDuplicates();
    } else {  // read Backports lsit
        backports_list = list.split("\n");
        backports_list.removeDuplicates();
    }
    return true;
}

// Cleanup environment when window is closed
void MainWindow::cleanup()
{
    if(!cmd->terminate()) {
        cmd->kill();
    }
    lock_file->unlock();
    if (tmp_dir.startsWith("/tmp/mxpm-")) {
        system("rm -r " + tmp_dir.toUtf8());
    }
}

// When the search is done
void MainWindow::closeSearch()
{
    ui->searchPopular->clear();
    ui->treeWidget->reset();
}


// Get version of the program
QString MainWindow::getVersion(QString name)
{
    return cmd->getOutput("dpkg -l "+ name + "| awk 'NR==6 {print $3}'");
}

// Return true if all the packages listed are installed
bool MainWindow::checkInstalled(const QString &names)
{
    if (names == "") {
        return false;
    }
    foreach(QString name, names.split("\n")) {
        if (!installed_packages.contains(name)) {
            return false;
        }
    }
    return true;
}

// Returns list of all installed packages
QStringList MainWindow::listInstalled()
{
    QString str = cmd->getOutput("dpkg --get-selections | grep -v deinstall | cut -f1");
    str.remove(":i386");
    str.remove(":amd64");
    return str.split("\n");
}

void MainWindow::cmdStart()
{
    setCursor(QCursor(Qt::BusyCursor));
}


void MainWindow::cmdDone()
{
    setCursor(QCursor(Qt::ArrowCursor));
    cmd->disconnect();
}

// Display info when clicking the "info" icon of the package
void MainWindow::displayInfo(QTreeWidgetItem *item, int column)
{
    if (column == 3 && item->childCount() == 0) {
        QString desc = item->text(4);
        QString install_names = item->text(5);
        QString title = item->text(2);
        QString msg = "<b>" + title + "</b><p>" + desc + "<p>" ;
        if (install_names != 0) {
            msg += tr("Packages to be installed: ") + install_names;
        }
        QUrl url = item->text(7); // screenshot url

        if (!url.isValid() || url.isEmpty() || url.url() == "none") {
            qDebug() << "no screenshot for: " << title;
        } else {
            QNetworkAccessManager *manager = new QNetworkAccessManager(this);
            QNetworkReply* reply = manager->get(QNetworkRequest(url));

            QEventLoop loop;
            connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
            timer->start(5000);
            connect(timer, &QTimer::timeout, &loop, &QEventLoop::quit);
            ui->treeWidget->blockSignals(true);
            loop.exec();
            timer->stop();
            ui->treeWidget->blockSignals(false);

            if (reply->error())
            {
                qDebug() << "Download of " << url.url() << " failed: " << qPrintable(reply->errorString());
            } else {
                QImage image;
                QByteArray data;
                QBuffer buffer(&data);
                QImageReader imageReader(reply);
                image = imageReader.read();
                if (imageReader.error()) {
                    qDebug() << "loading screenshot: " << imageReader.errorString();
                } else {
                    image = image.scaled(QSize(200,300), Qt::KeepAspectRatioByExpanding);
                    image.save(&buffer, "PNG");
                    msg += QString("<p><img src='data:image/png;base64, %0'>").arg(QString(data.toBase64()));
                }
            }
        }

        QMessageBox info(QMessageBox::NoIcon, tr("Package info") , msg, QMessageBox::Close, this);
        info.exec();
    }
}

// Find package in view
void MainWindow::findPackage()
{
    QTreeWidgetItemIterator it(ui->treeWidget);
    QString word = ui->searchPopular->text();
    if (word == "") {
        while (*it) {
            (*it)->setExpanded(false);
            ++it;
        }
        ui->treeWidget->reset();
        return;
    }
    QList<QTreeWidgetItem *> found_items = ui->treeWidget->findItems(word, Qt::MatchContains|Qt::MatchRecursive, 2);

    // hide/unhide items
    while (*it) {
        if ((*it)->childCount() == 0) { // if child
            if (found_items.contains(*it)) {
                (*it)->setHidden(false);
          } else {
                (*it)->parent()->setExpanded(false);
                (*it)->setHidden(true);
            }
        }
        ++it;
    }

    // process found items
    foreach(QTreeWidgetItem *item, found_items) {
        if (item->childCount() == 0) { // if child, expand parent
            item->parent()->setExpanded(true);
        } else {  // if parent, expand children
            item->setExpanded(true);
            int count = item->childCount();
            for (int i = 0; i < count; i++ ) {
                item->child(i)->setHidden(false);
            }
        }
    }


}

// Install button clicked
void MainWindow::on_buttonInstall_clicked()
{
    if (ui->tabApps->isVisible()) {
        installPopularApps();
    }

}

// About button clicked
void MainWindow::on_buttonAbout_clicked()
{
    this->hide();
    QMessageBox msgBox(QMessageBox::NoIcon,
                       tr("About MX Package Manager"), "<p align=\"center\"><b><h2>" +
                       tr("MX Package Manager") + "</h2></b></p><p align=\"center\">" + tr("Version: ") + version + "</p><p align=\"center\"><h3>" +
                       tr("Package Manager for MX Linux") +
                       "</h3></p><p align=\"center\"><a href=\"http://mxlinux.org\">http://mxlinux.org</a><br /></p><p align=\"center\">" +
                       tr("Copyright (c) MX Linux") + "<br /><br /></p>", 0, this);
    msgBox.addButton(tr("Cancel"), QMessageBox::AcceptRole); // because we want to display the buttons in reverse order we use counter-intuitive roles.
    msgBox.addButton(tr("License"), QMessageBox::RejectRole);
    if (msgBox.exec() == QMessageBox::RejectRole) {
        system("mx-viewer file:///usr/share/doc/mx-package-manager/license.html '" + tr("MX Package Manager").toUtf8() + " " + tr("License").toUtf8() + "'");
    }
    this->show();
}

// Help button clicked
void MainWindow::on_buttonHelp_clicked()
{
    this->hide();
    QString cmd = QString("mx-viewer https://mxlinux.org/user_manual_mx15/mxum.html#test '%1'").arg(tr("MX Package Manager"));
    system(cmd.toUtf8());
    this->show();
}

// Resize columns when expanding
void MainWindow::on_treeWidget_expanded()
{
    ui->treeWidget->resizeColumnToContents(2);
    ui->treeWidget->resizeColumnToContents(4);
}

// Tree item clicked
void MainWindow::on_treeWidget_itemClicked()
{
    bool checked = false;
    bool installed = true;

    QTreeWidgetItemIterator it(ui->treeWidget);
    while (*it) {
        if ((*it)->checkState(1) == Qt::Checked) {
            checked = true;
            if ((*it)->foreground(2) != Qt::gray) {
                installed = false;
            }
        }
        ++it;
    }
    ui->buttonInstall->setEnabled(checked);
    ui->buttonUninstall->setEnabled(checked && installed);
    if (checked && installed) {
        ui->buttonInstall->setText(tr("Reinstall"));
    } else {
        ui->buttonInstall->setText(tr("Install"));
    }
}

// Tree item expanded
void MainWindow::on_treeWidget_itemExpanded(QTreeWidgetItem *item)
{
    item->setIcon(0, QIcon::fromTheme("folder-open"));
    ui->treeWidget->resizeColumnToContents(4);
}

// Tree item collapsed
void MainWindow::on_treeWidget_itemCollapsed(QTreeWidgetItem *item)
{
    item->setIcon(0, QIcon::fromTheme("folder-green"));
    ui->treeWidget->resizeColumnToContents(4);
}


// Uninstall clicked
void MainWindow::on_buttonUninstall_clicked()
{
    QString names;
    QTreeWidgetItemIterator it(ui->treeWidget);
    while (*it) {
        if ((*it)->checkState(1) == Qt::Checked) {
            names += (*it)->text(6).replace("\n", " ") + " ";
        }
        ++it;
    }
    uninstall(names);
}

// Actions on switching the tabs
void MainWindow::on_tabWidget_currentChanged(int index)
{
    if (index == 1) {
        buildPackageLists();
    }
}

