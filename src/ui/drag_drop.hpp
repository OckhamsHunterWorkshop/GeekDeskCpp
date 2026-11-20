#pragma once

#include <oleidl.h>
#include <windows.h>

#include <filesystem>
#include <functional>
#include <vector>

namespace geekdesk::ui {

struct DropTargetCallbacks {
    std::function<bool(POINT screenPt)> onHover;
    std::function<void()> onLeave;
    std::function<void(POINT screenPt, const std::vector<std::filesystem::path>& paths)> onDrop;
};

/// Single-target OLE IDropTarget for CF_HDROP external file/shortcut drops with re-entrancy guard.
class ExternalDropTarget final : public IDropTarget {
public:
    explicit ExternalDropTarget(DropTargetCallbacks callbacks);
    ~ExternalDropTarget() = default;

    // IUnknown
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** ppvObject) override;
    ULONG STDMETHODCALLTYPE AddRef() override;
    ULONG STDMETHODCALLTYPE Release() override;

    // IDropTarget
    HRESULT STDMETHODCALLTYPE DragEnter(IDataObject* pDataObj,
                                        DWORD grfKeyState,
                                        POINTL pt,
                                        DWORD* pdwEffect) override;
    HRESULT STDMETHODCALLTYPE DragOver(DWORD grfKeyState,
                                       POINTL pt,
                                       DWORD* pdwEffect) override;
    HRESULT STDMETHODCALLTYPE DragLeave() override;
    HRESULT STDMETHODCALLTYPE Drop(IDataObject* pDataObj,
                                   DWORD grfKeyState,
                                   POINTL pt,
                                   DWORD* pdwEffect) override;

private:
    static bool HasFileDropFormat(IDataObject* pDataObj);
    static std::vector<std::filesystem::path> ExtractFilePaths(IDataObject* pDataObj);

    ULONG refCount_ = 1;
    DropTargetCallbacks callbacks_;
    bool hasFiles_ = false;
    bool handlingDrop_ = false;
};

} // namespace geekdesk::ui
