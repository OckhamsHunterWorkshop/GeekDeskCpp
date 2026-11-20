#include "ui/drag_drop.hpp"

#include <shellapi.h>

namespace geekdesk::ui {

ExternalDropTarget::ExternalDropTarget(DropTargetCallbacks callbacks)
    : callbacks_(std::move(callbacks)) {}

HRESULT STDMETHODCALLTYPE ExternalDropTarget::QueryInterface(REFIID riid, void** ppvObject) {
    if (ppvObject == nullptr) {
        return E_POINTER;
    }
    if (riid == IID_IUnknown || riid == IID_IDropTarget) {
        *ppvObject = static_cast<IDropTarget*>(this);
        AddRef();
        return S_OK;
    }
    *ppvObject = nullptr;
    return E_NOINTERFACE;
}

ULONG STDMETHODCALLTYPE ExternalDropTarget::AddRef() {
    return ++refCount_;
}

ULONG STDMETHODCALLTYPE ExternalDropTarget::Release() {
    const ULONG next = --refCount_;
    if (next == 0) {
        delete this;
    }
    return next;
}

bool ExternalDropTarget::HasFileDropFormat(IDataObject* pDataObj) {
    if (pDataObj == nullptr) {
        return false;
    }
    FORMATETC fmt{CF_HDROP, nullptr, DVASPECT_CONTENT, -1, TYMED_HGLOBAL};
    return SUCCEEDED(pDataObj->QueryGetData(&fmt));
}

std::vector<std::filesystem::path> ExternalDropTarget::ExtractFilePaths(IDataObject* pDataObj) {
    std::vector<std::filesystem::path> out;
    if (pDataObj == nullptr) {
        return out;
    }
    FORMATETC fmt{CF_HDROP, nullptr, DVASPECT_CONTENT, -1, TYMED_HGLOBAL};
    STGMEDIUM stg{};
    if (FAILED(pDataObj->GetData(&fmt, &stg))) {
        return out;
    }

    if (HDROP hDrop = static_cast<HDROP>(GlobalLock(stg.hGlobal)); hDrop != nullptr) {
        const UINT count = DragQueryFileW(hDrop, 0xFFFFFFFFu, nullptr, 0);
        out.reserve(count);
        for (UINT i = 0; i < count; ++i) {
            const UINT len = DragQueryFileW(hDrop, i, nullptr, 0);
            if (len == 0) {
                continue;
            }
            std::wstring pathBuf(len + 1, L'\0');
            if (DragQueryFileW(hDrop, i, pathBuf.data(), len + 1) > 0) {
                pathBuf.resize(len);
                out.emplace_back(std::move(pathBuf));
            }
        }
        GlobalUnlock(stg.hGlobal);
    }
    ReleaseStgMedium(&stg);
    return out;
}

HRESULT STDMETHODCALLTYPE ExternalDropTarget::DragEnter(IDataObject* pDataObj,
                                                        DWORD grfKeyState,
                                                        POINTL pt,
                                                        DWORD* pdwEffect) {
    hasFiles_ = HasFileDropFormat(pDataObj);
    return DragOver(grfKeyState, pt, pdwEffect);
}

HRESULT STDMETHODCALLTYPE ExternalDropTarget::DragOver(DWORD, POINTL pt, DWORD* pdwEffect) {
    if (pdwEffect == nullptr) {
        return E_POINTER;
    }
    if (!hasFiles_) {
        *pdwEffect = DROPEFFECT_NONE;
        return S_OK;
    }
    const POINT screenPt{pt.x, pt.y};
    const bool accepted = callbacks_.onHover ? callbacks_.onHover(screenPt) : false;
    *pdwEffect = accepted ? DROPEFFECT_LINK : DROPEFFECT_NONE;
    return S_OK;
}

HRESULT STDMETHODCALLTYPE ExternalDropTarget::DragLeave() {
    hasFiles_ = false;
    if (callbacks_.onLeave) {
        callbacks_.onLeave();
    }
    return S_OK;
}

HRESULT STDMETHODCALLTYPE ExternalDropTarget::Drop(IDataObject* pDataObj,
                                                   DWORD,
                                                   POINTL pt,
                                                   DWORD* pdwEffect) {
    if (pdwEffect == nullptr) {
        return E_POINTER;
    }
    if (handlingDrop_ || !hasFiles_) {
        *pdwEffect = DROPEFFECT_NONE;
        return S_OK;
    }

    handlingDrop_ = true;
    const auto paths = ExtractFilePaths(pDataObj);
    const POINT screenPt{pt.x, pt.y};
    if (!paths.empty() && callbacks_.onDrop) {
        callbacks_.onDrop(screenPt, paths);
        *pdwEffect = DROPEFFECT_LINK;
    } else {
        *pdwEffect = DROPEFFECT_NONE;
    }
    hasFiles_ = false;
    if (callbacks_.onLeave) {
        callbacks_.onLeave();
    }
    handlingDrop_ = false;
    return S_OK;
}

} // namespace geekdesk::ui
