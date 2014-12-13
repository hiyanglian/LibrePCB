/*
 * EDA4U - Professional EDA for everyone!
 * Copyright (C) 2013 Urban Bruhin
 * http://eda4u.ubruhin.ch/
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

/*****************************************************************************************
 *  Includes
 ****************************************************************************************/

#include <QtCore>
#include "inifile.h"
#include "exceptions.h"
#include "filepath.h"

/*****************************************************************************************
 *  Constructors / Destructor
 ****************************************************************************************/

IniFile::IniFile(const FilePath& filepath, bool restore, bool readOnly) throw (Exception) :
    QObject(0), mFilepath(filepath), mTmpFilepath(), mIsReadOnly(readOnly), mSettings(),
    mFileVersion(-1)
{
    // decide if we open the original file (*.ini) or the backup (*.ini~)
    FilePath iniFilepath(mFilepath.toStr() % '~');
    if ((!restore) || (!iniFilepath.isExistingFile()))
        iniFilepath = mFilepath;

    // check if the file exists
    if (!iniFilepath.isExistingFile())
    {
        throw RuntimeError(__FILE__, __LINE__, iniFilepath.toStr(),
            QString(tr("The file \"%1\" does not exist!")).arg(iniFilepath.toNative()));
    }

    // create a unique filename in the operating system's temporary directory
    QString tmpFilename = QCryptographicHash::hash(iniFilepath.toStr().toLocal8Bit(),
                            QCryptographicHash::Sha256).toBase64(
                            QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals);
    mTmpFilepath.setPath(QDir::temp().absoluteFilePath("EDA4U/" % tmpFilename));
    mTmpFilepath.getParentDir().mkPath();

    // Remove the temporary file if it exists already
    if (mTmpFilepath.isExistingFile())
    {
        if (!QFile::remove(mTmpFilepath.toStr()))
        {
            throw RuntimeError(__FILE__, __LINE__, mTmpFilepath.toStr(),
                QString(tr("Could not remove file \"%1\"!")).arg(mTmpFilepath.toNative()));
        }
    }

    // Copy the INI file to the temporary file
    if (!QFile::copy(iniFilepath.toStr(), mTmpFilepath.toStr()))
    {
        throw RuntimeError(__FILE__, __LINE__,
            QString("%1:%2").arg(iniFilepath.toStr(), mTmpFilepath.toStr()),
            QString(tr("Could not copy file \"%1\" to \"%2\"!"))
            .arg(iniFilepath.toNative(), mTmpFilepath.toNative()));
    }

    // Read the file version
    QSettings* s = createQSettings();
    bool ok;
    int version = s->value("meta/file_version").toInt(&ok);
    mFileVersion = ok ? version : -1;
    releaseQSettings(s);
}

IniFile::~IniFile() noexcept
{
    if (!mSettings.isEmpty())
    {
        qWarning() << "mSettings still contains" << mSettings.count() << "elements!";
        qDeleteAll(mSettings);      mSettings.clear();
    }

    // remove temporary files
    QFile::remove(mTmpFilepath.toStr());
    if (!mIsReadOnly)
        QFile::remove(mFilepath.toStr() % "~");
}

/*****************************************************************************************
 *  Setters
 ****************************************************************************************/

void IniFile::setFileVersion(int version) throw (Exception)
{
    QSettings* s = createQSettings();
    // Maybe we do not need to convert the integer to a QString explicitely because
    // QVariant can do this also. But as the method QString::number(int) is
    // locale-independent for sure, we use that method to be on the save site.
    s->setValue("meta/file_version", QString::number(version));
    mFileVersion = version;
    releaseQSettings(s);
}

/*****************************************************************************************
 *  General Methods
 ****************************************************************************************/

QSettings* IniFile::createQSettings() throw (Exception)
{
    QSettings* settings = 0;

    try
    {
        settings = new QSettings(mTmpFilepath.toStr(), QSettings::IniFormat);
        if (settings->status() != QSettings::NoError)
        {
            throw RuntimeError(__FILE__, __LINE__, mTmpFilepath.toStr(),
                QString(tr("Error while opening file \"%1\"!")).arg(mTmpFilepath.toNative()));
        }
    }
    catch (Exception& e)
    {
        delete settings;    settings = 0;
        throw;
    }

    mSettings.append(settings);
    return settings;
}

void IniFile::releaseQSettings(QSettings* settings) noexcept
{
    Q_ASSERT(settings);
    Q_ASSERT(mSettings.contains(settings));

    settings->sync();
    if (settings->status() == QSettings::NoError)
    {
        mSettings.removeOne(settings);
        delete settings;
    }

    // if sync() was not successful, we let the QSettings object alive and hold the
    // pointer in the mSettings list. This way, the method save() can also detect the error
    // and then throws an exception. The destructor then will delete the QSettings object.
}

void IniFile::remove() const throw (Exception)
{
    bool success = true;

    if (mIsReadOnly)
        throw LogicError(__FILE__, __LINE__, QString(), tr("Cannot remove read-only file!"));

    if (QFile::exists(mFilepath.toStr()))
    {
        if (!QFile::remove(mFilepath.toStr()))
            success = false;
    }

    if (QFile::exists(mFilepath.toStr() % '~'))
    {
        if (!QFile::remove(mFilepath.toStr() % '~'))
            success = false;
    }

    if (mSettings.isEmpty())
    {
        if (QFile::exists(mTmpFilepath.toStr()))
        {
            if (!QFile::remove(mTmpFilepath.toStr()))
                success = false;
        }
    }
    else
        qCritical() << "mSettings is not empty:" << mSettings.count();

    if (!success)
    {
        throw RuntimeError(__FILE__, __LINE__, mFilepath.toStr(),
            QString(tr("Could not remove file \"%1\"")).arg(mFilepath.toNative()));
    }
}

void IniFile::save(bool toOriginal) throw (Exception)
{
    if (mIsReadOnly)
        throw LogicError(__FILE__, __LINE__, QString(), tr("Cannot save read-only file!"));

    FilePath filepath(toOriginal ? mFilepath.toStr() : mFilepath.toStr() % '~');

    foreach (QSettings* settings, mSettings)
    {
        settings->sync(); // write all changes to the temp file in temp directory

        if (settings->status() != QSettings::NoError)
        {
            throw RuntimeError(__FILE__, __LINE__, filepath.toStr(),
                QString(tr("Error while writing to file \"%1\"!")).arg(filepath.toNative()));
        }
    }

    // remove the target file
    if (filepath.isExistingFile())
    {
        if (!QFile::remove(filepath.toStr()))
        {
            throw RuntimeError(__FILE__, __LINE__, filepath.toStr(),
                QString(tr("Could not remove file \"%1\"!")).arg(filepath.toNative()));
        }
    }

    // copy the temp file in the temp directory to the original directory
    if (!QFile::copy(mTmpFilepath.toStr(), filepath.toStr()))
    {
        throw RuntimeError(__FILE__, __LINE__,
            QString("%1:%2").arg(mTmpFilepath.toStr(), filepath.toStr()),
            QString(tr("Could not copy file \"%1\" to \"%2\"!"))
            .arg(mTmpFilepath.toNative(), filepath.toNative()));
    }

    // check if the target file exists
    if (!filepath.isExistingFile())
    {
        throw RuntimeError(__FILE__, __LINE__, filepath.toStr(),
            QString(tr("Error while writing to file \"%1\"!")).arg(filepath.toNative()));
    }
}

/*****************************************************************************************
 *  Static Methods
 ****************************************************************************************/

IniFile* IniFile::create(const FilePath& filepath, int version) throw (Exception)
{
    // remove the file if it exists already
    if (filepath.isExistingFile())
    {
        if (!QFile::remove(filepath.toStr()))
        {
            throw RuntimeError(__FILE__, __LINE__, filepath.toStr(),
                QString(tr("Cannot remove file \"%1\"")).arg(filepath.toNative()));
        }
    }

    // create all parent directories
    if (!filepath.getParentDir().mkPath())
    {
        throw RuntimeError(__FILE__, __LINE__, filepath.toStr(), QString(tr("Cannot "
            "create directory \"%1\"!")).arg(filepath.getParentDir().toNative()));
    }

    // create an empty temporary file
    QFile file(filepath.toStr() % '~');
    if (!file.open(QIODevice::WriteOnly))
    {
        throw RuntimeError(__FILE__, __LINE__, filepath.toStr(), QString(tr("Cannot "
            "create file \"%1\": %2")).arg(filepath.toNative(), file.errorString()));
    }
    file.close();

    // open and return the new INI file object
    IniFile* obj = new IniFile(filepath, true, false);
    if (version > -1)
    {
        try
        {
            obj->setFileVersion(version);
            obj->save(false); // save to temporary file
        }
        catch (Exception& e)
        {
            delete obj;
            throw;
        }
    }
    return obj;
}

/*****************************************************************************************
 *  End of File
 ****************************************************************************************/