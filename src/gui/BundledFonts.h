#pragma once
// ============================================================
// BundledFonts.h — Phase 119.1: the fonts the app asks for, carried
// inside it
//
// The source named Barlow Condensed and Space Mono 47 times and neither
// was on the machine. Nothing complained, because neither GDI nor
// DirectWrite does: they substitute and go on. So the typography phases
// 92 and 96 tuned had never been on a screen.
//
// Both are SIL Open Font License, with the notices in assets/fonts. They
// are embedded as resources, so there is no install step and no path to
// lose, and they are private to this process.
//
// ── Two text stacks, two doors ──────────────────────────────
//
// GDI takes them through AddFontMemResourceEx. DirectWrite does not see
// those: it needs its own collection, passed where a text format is
// made, so every CreateTextFormat goes through FamilyFor and
// CollectionFor. The second returns null for any other face, which is DirectWrite's word for the
// system fonts, so Segoe UI keeps working. If loading failed it is null
// for everything, and the app looks as it always has.
// ============================================================

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <dwrite_3.h>

#include <cwchar>
#include <vector>

#pragma comment(lib, "dwrite.lib")
#pragma comment(lib, "gdi32.lib")

namespace raduk::gui::Fonts {

struct Blob { const void* data; DWORD size; };

namespace detail {
    inline IDWriteFontCollection1*& Collection() {
        static IDWriteFontCollection1* c = nullptr;
        return c;
    }
}

// Both doors, for fonts already in memory. How many loaded.
inline int Register(const std::vector<Blob>& fonts) {
    int ok = 0;
    for (const Blob& b : fonts) {
        DWORD count = 0;
        if (AddFontMemResourceEx(const_cast<void*>(b.data), b.size, nullptr, &count))
            ++ok;
    }

    IDWriteFactory5* f = nullptr;
    if (FAILED(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED,
                                   __uuidof(IDWriteFactory5),
                                   reinterpret_cast<IUnknown**>(&f))))
        return ok;
    IDWriteInMemoryFontFileLoader* loader = nullptr;
    IDWriteFontSetBuilder1* builder = nullptr;
    if (SUCCEEDED(f->CreateInMemoryFontFileLoader(&loader)) &&
        SUCCEEDED(f->RegisterFontFileLoader(loader)) &&
        SUCCEEDED(f->CreateFontSetBuilder(&builder))) {
        for (const Blob& b : fonts) {
            IDWriteFontFile* file = nullptr;
            // No owner, so the loader keeps its own copy.
            if (SUCCEEDED(loader->CreateInMemoryFontFileReference(
                    f, b.data, b.size, nullptr, &file))) {
                builder->AddFontFile(file);
                file->Release();
            }
        }
        IDWriteFontSet* set = nullptr;
        if (SUCCEEDED(builder->CreateFontSet(&set))) {
            f->CreateFontCollectionFromFontSet(set, &detail::Collection());
            set->Release();
        }
    }
    // The loader stays registered and the factory alive: the collection
    // reads through them for the life of the process.
    if (builder) builder->Release();
    return ok;
}

// From this module's resources. RCDATA 301 onward, until one is missing.
inline int LoadBundled() {
    std::vector<Blob> fonts;
    const HMODULE mod = GetModuleHandleW(nullptr);
    for (int id = 301; id < 320; ++id) {
        HRSRC r = FindResourceW(mod, MAKEINTRESOURCEW(id), MAKEINTRESOURCEW(10));   // RT_RCDATA
        if (!r) break;
        HGLOBAL g = LoadResource(mod, r);
        const void* p = g ? LockResource(g) : nullptr;
        if (p) fonts.push_back({p, SizeofResource(mod, r)});
    }
    return fonts.empty() ? 0 : Register(fonts);
}

// What to pass as CreateTextFormat's family for this face. GDI knows the
// name the whole app uses. DirectWrite files the same font as family
// "Barlow" at a condensed width, under every family model, and finds
// nothing by the other name. The collection holds only condensed Barlow,
// so a request at normal width lands on it.
inline const wchar_t* FamilyFor(const wchar_t* face) {
    if (detail::Collection() && face && wcscmp(face, L"Barlow Condensed") == 0)
        return L"Barlow";
    return face;
}

// What to pass as CreateTextFormat's collection for this face.
inline IDWriteFontCollection* CollectionFor(const wchar_t* face) {
    IDWriteFontCollection1* c = detail::Collection();
    if (!c || !face) return nullptr;
    return (wcscmp(face, L"Barlow Condensed") == 0 ||
            wcscmp(face, L"Space Mono") == 0) ? c : nullptr;
}

} // namespace raduk::gui::Fonts
