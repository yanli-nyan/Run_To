#ifndef APPICONPROVIDER_H
#define APPICONPROVIDER_H

#include <QQuickImageProvider>
#include <QCache>
#include <QImage>
#include <QMutex>

class AppIconProvider : public QQuickImageProvider
{
public:
    AppIconProvider();
    // 核心重写方法：QML 请求图片时会调用此函数
    QImage requestImage(const QString &id, QSize *size, const QSize &requestedSize) override;

private:
    // 图标提取要跑 CoInitialize + IShellLinkW + PrivateExtractIconsW + DIB 拷贝，
    // 单次开销不小；QML 侧的 QQuickPixmapCache 又只对 cache:true 的 Image 生效，
    // 详情页显式写了 cache:false，改一次路径就会重新抽一遍。
    // 这里按「路径 + 尺寸」缓存已解出的 QImage，命中时直接返回，避免重复 COM 调用。
    //
    // 容量按字节计：64x64 ARGB32 约 16KB，4MB 上限约合 250 个图标，
    // 对「模块卡片 + 应用扫描列表」这个规模足够，且不会随扫描条目数无限增长。
    static constexpr int kMaxCacheBytes = 4 * 1024 * 1024;

    mutable QMutex m_cacheMutex;
    QCache<QString, QImage> m_cache;
};

#endif // APPICONPROVIDER_H
