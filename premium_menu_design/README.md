# Referensi desain menu

Prototipe React/Vite ini menyimpan referensi UI dan katalog desain 47 fitur. Menu Android produksi berada di [src/wsm-v2/menu](../src/wsm-v2/menu).

`src/data.ts` adalah input aktif [generator katalog](../src/wsm-v2/scripts/build_feature_catalog.py). Generator memeriksa hash dan semantik desain; perubahan file tersebut memerlukan pembaruan snapshot/evidence yang sesuai. Direktori ini tetap diperlukan pada clone publik dengan `-CatalogSnapshot`.

`package.json` dan lockfile mendeskripsikan lingkungan prototipe. Menjalankan prototipe web tidak diperlukan untuk membangun modul Android. Panduan utama: [build WSM](../docs/BUILDING.md), [matriks fitur](../docs/FEATURES.md), dan [notice dependensi](../THIRD_PARTY_NOTICES.md).
