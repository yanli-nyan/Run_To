#include "ShareManager.h"

#include <QCoreApplication>
#include <QFile>
#include <QFileInfo>
#include <QDir>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QFileIconProvider>
#include <QIcon>
#include <QPixmap>
#include <QBuffer>
#include <QPainter>
#include <QFont>
#include <QLinearGradient>
#include <QColor>

// 区分 Qt5 与 Qt6 的 QZipWriter 路径
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
#include <QtCore/private/qzipwriter_p.h>
#else
#include <QtGui/private/qzipwriter_p.h>
#endif

#ifdef Q_OS_WIN
#include <windows.h>
#include <shobjidl.h>
#include <shlguid.h>

// 根据名称/标识符和主题色，动态绘制 256x256 高清圆角 App 图标
static QPixmap generateModernLetterIcon(const QString &text, const QString &colorHex, int targetSize = 256)
{
    // 创建带 Alpha 通道的离屏画布
    QImage image(targetSize, targetSize, QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::transparent);

    QPainter painter(&image);
    painter.setRenderHints(QPainter::Antialiasing | QPainter::TextAntialiasing | QPainter::SmoothPixmapTransform);

    // 1. 颜色与微渐变处理（上下微调明度，呈现现代光影质感）
    QColor baseColor(colorHex.trimmed().isEmpty() ? "#3B82F6" : colorHex.trimmed());
    if (!baseColor.isValid())
    {
        baseColor = QColor("#3B82F6");
    }

    QLinearGradient gradient(0, 0, 0, targetSize);
    gradient.setColorAt(0.0, baseColor.lighter(112));
    gradient.setColorAt(1.0, baseColor.darker(108));

    // 2. 绘制圆角矩形底板（圆角半径约为宽度的 22%，贴合现代 UI 图标规范）
    qreal cornerRadius = targetSize * 0.22;
    QRectF rect(0, 0, targetSize, targetSize);
    painter.setPen(Qt::NoPen);
    painter.setBrush(gradient);
    painter.drawRoundedRect(rect, cornerRadius, cornerRadius);

    // 3. 提取中心文字（优先 1 个主字；若是英文缩写最多支持 2 个字符）
    QString displayText = text.trimmed();
    if (displayText.isEmpty())
    {
        displayText = QStringLiteral("M");
    }
    else if (displayText.length() > 2)
    {
        displayText = displayText.left(1);
    }

    // 4. 字体设置与文字垂直居中排版
    QFont font = painter.font();
    font.setBold(true);
    // 单字占比约 46%，双字占比约 32%
    int pixelSize = (displayText.length() == 1) ? (targetSize * 0.46) : (targetSize * 0.32);
    font.setPixelSize(pixelSize);
    font.setFamily(QStringLiteral("Microsoft YaHei, PingFang SC, Segoe UI, sans-serif"));
    painter.setFont(font);

    // 白色微透文字
    painter.setPen(QColor(255, 255, 255, 245));
    painter.drawText(rect, Qt::AlignCenter, displayText);

    painter.end();
    return QPixmap::fromImage(image);
}

// 将 Windows 原生 HICON 转换为带 Alpha 通道的 32 位 QImage
static QImage hIconToQImage(HICON hIcon)
{
    if (!hIcon)
        return QImage();

    ICONINFO iconInfo;
    if (!GetIconInfo(hIcon, &iconInfo))
        return QImage();

    BITMAP bmp;
    GetObject(iconInfo.hbmColor, sizeof(BITMAP), &bmp);

    int width = bmp.bmWidth;
    int height = bmp.bmHeight;

    BITMAPINFO bi = {0};
    bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth = width;
    bi.bmiHeader.biHeight = -height;
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    bi.bmiHeader.biCompression = BI_RGB;

    QImage image(width, height, QImage::Format_ARGB32_Premultiplied);
    HDC hdc = GetDC(nullptr);
    GetDIBits(hdc, iconInfo.hbmColor, 0, height, image.bits(), &bi, DIB_RGB_COLORS);

    bool hasAlpha = false;
    const QRgb *pixels = reinterpret_cast<const QRgb *>(image.constBits());
    int pixelCount = width * height;
    for (int i = 0; i < pixelCount; ++i)
    {
        if (qAlpha(pixels[i]) != 0)
        {
            hasAlpha = true;
            break;
        }
    }

    if (!hasAlpha && iconInfo.hbmMask)
    {
        QImage mask(width, height, QImage::Format_Mono);
        GetDIBits(hdc, iconInfo.hbmMask, 0, height, mask.bits(), &bi, DIB_RGB_COLORS);
        QRgb *scanLine = reinterpret_cast<QRgb *>(image.bits());
        for (int y = 0; y < height; ++y)
        {
            for (int x = 0; x < width; ++x)
            {
                if (mask.pixelIndex(x, y) != 0)
                {
                    scanLine[y * width + x] = 0;
                }
                else
                {
                    scanLine[y * width + x] |= 0xFF000000;
                }
            }
        }
    }

    ReleaseDC(nullptr, hdc);
    DeleteObject(iconInfo.hbmColor);
    DeleteObject(iconInfo.hbmMask);

    return image;
}

// 从文件（支持 .lnk 解析）直接提取无小箭头的高清图标
static QPixmap extractCleanAppPixmap(const QString &filePath, int targetSize = 256)
{
    QString cleanPath = QDir::toNativeSeparators(filePath);
    QString targetFile = cleanPath;
    int iconIndex = 0;

    CoInitialize(nullptr);

    // 若是快捷方式，解析出实际指向或绑定的图标文件
    if (cleanPath.endsWith(".lnk", Qt::CaseInsensitive))
    {
        IShellLinkW *psl = nullptr;
        if (SUCCEEDED(CoCreateInstance(CLSID_ShellLink, nullptr, CLSCTX_INPROC_SERVER, IID_IShellLinkW, (void **)&psl)))
        {
            IPersistFile *ppf = nullptr;
            if (SUCCEEDED(psl->QueryInterface(IID_IPersistFile, (void **)&ppf)))
            {
                if (SUCCEEDED(ppf->Load(reinterpret_cast<const wchar_t *>(cleanPath.utf16()), STGM_READ)))
                {
                    wchar_t iconPath[MAX_PATH] = {0};
                    int idx = 0;
                    psl->GetIconLocation(iconPath, MAX_PATH, &idx);
                    if (wcslen(iconPath) > 0)
                    {
                        wchar_t expanded[MAX_PATH] = {0};
                        ExpandEnvironmentStringsW(iconPath, expanded, MAX_PATH);
                        targetFile = QString::fromWCharArray(expanded);
                        iconIndex = idx;
                    }
                    else
                    {
                        wchar_t target[MAX_PATH] = {0};
                        WIN32_FIND_DATAW wfd;
                        if (SUCCEEDED(psl->GetPath(target, MAX_PATH, &wfd, SLGP_UNCPRIORITY)))
                        {
                            targetFile = QString::fromWCharArray(target);
                            iconIndex = 0;
                        }
                    }
                }
                ppf->Release();
            }
            psl->Release();
        }
    }

    // 直接提取 PE 裸图标（绝无小箭头，且支持 256x256 高清原图）
    HICON hIcon = nullptr;
    UINT iconId = 0;
    UINT count = PrivateExtractIconsW(
        reinterpret_cast<const wchar_t *>(targetFile.utf16()),
        iconIndex,
        targetSize,
        targetSize,
        &hIcon,
        &iconId,
        1,
        LR_LOADFROMFILE);

    CoUninitialize();

    if (count > 0 && hIcon)
    {
        QImage img = hIconToQImage(hIcon);
        DestroyIcon(hIcon);
        if (!img.isNull())
        {
            return QPixmap::fromImage(img);
        }
    }

    return QPixmap();
}
#endif

ShareManager::ShareManager(QObject *parent) : QObject(parent) {}

bool ShareManager::exportSharePackage(const QString &outputZipPath)
{
    QString appDir = QCoreApplication::applicationDirPath();
    QString configPath = appDir + "/config.json";

    // 1. 读取 config.json
    QFile configFile(configPath);
    if (!configFile.open(QIODevice::ReadOnly))
    {
        emit exportFinished(false, QString("未找到配置文件：%1").arg(configPath));
        return false;
    }

    QByteArray configBytes = configFile.readAll();
    configFile.close();

    QJsonParseError err;
    QJsonDocument doc = QJsonDocument::fromJson(configBytes, &err);
    if (err.error != QJsonParseError::NoError || !doc.isObject())
    {
        emit exportFinished(false, QString("JSON 解析错误：%1").arg(err.errorString()));
        return false;
    }

    // 2. 准备输出 ZIP 路径
    QString targetZip = outputZipPath;
    if (targetZip.isEmpty())
    {
        targetZip = appDir + "/share_bundle.zip";
    }

    QZipWriter zip(targetZip);
    if (zip.status() != QZipWriter::NoError)
    {
        emit exportFinished(false, QString("创建 ZIP 失败，请检查写入权限：%1").arg(targetZip));
        return false;
    }

    // 3. 将原始 config.json 写入 ZIP 根目录
    zip.addFile("config.json", configBytes);

    // 4. 解析 modules 并提取/生成各子项图标
    QJsonObject rootObj = doc.object();
    QJsonArray modules = rootObj.value("modules").toArray();
    QFileIconProvider iconProvider;

    for (const QJsonValue &item : modules)
    {
        QJsonObject mod = item.toObject();
        QString name = mod.value("name").toString();
        QString character = mod.value("character").toString();
        QString rawPath = mod.value("path").toString().trimmed();
        QString colorHex = mod.value("color").toString("#3B82F6");

        // 若 character 为空，则优先取 name 的首字符作为标识与文件名
        if (character.isEmpty())
        {
            character = name.trimmed().isEmpty() ? QStringLiteral("M") : name.trimmed().left(1);
        }

        QPixmap pixmap;

        // 仅在配置了有效路径且路径存在时尝试提取原生图标
        if (!rawPath.isEmpty())
        {
            QString resolvedPath = rawPath;
            QFileInfo fileInfo(resolvedPath);
            if (fileInfo.isRelative())
            {
                resolvedPath = QDir(appDir).filePath(rawPath);
                fileInfo.setFile(resolvedPath);
            }

            if (fileInfo.exists())
            {
#ifdef Q_OS_WIN
                // 优先使用 Windows API 提取无快捷方式角标的高清大图
                pixmap = extractCleanAppPixmap(resolvedPath, 256);
#endif
                // 回退走系统图标库
                if (pixmap.isNull())
                {
                    QIcon icon = iconProvider.icon(fileInfo);
                    if (!icon.isNull())
                    {
                        pixmap = icon.pixmap(256, 256);
                    }
                }
            }
        }

        // 最终兜底：没有路径、提取失败或无原生图标时，根据 character / name 与主题色动态绘制
        if (pixmap.isNull())
        {
            QString letter = !character.trimmed().isEmpty() ? character : name;
            pixmap = generateModernLetterIcon(letter, colorHex, 256);
        }

        // 转成 PNG 数据流并存入 resources/<character>.png
        QByteArray imgBytes;
        QBuffer buffer(&imgBytes);
        buffer.open(QIODevice::WriteOnly);
        pixmap.save(&buffer, "PNG");

        QString zipEntryPath = QString("resources/%1.png").arg(character);
        zip.addFile(zipEntryPath, imgBytes);
    }

    zip.close();
    emit exportFinished(true, QString("打包成功，已生成：%1").arg(targetZip));
    return true;
}