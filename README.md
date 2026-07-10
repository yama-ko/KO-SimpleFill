# KO SimpleFill

After Effectsのプラグイン。レイヤーを指定した色で塗りつぶします。複数のブレンドモードに対応。

**[最新版をダウンロード (Releases)](https://github.com/yama-ko/KO-SimpleFill/releases/latest)**

## 機能

- **ブレンドモード 10種**: Normal / Add / Negative Add / Multiply / Screen / Overlay / Hard Light / Lighten / Darken / Difference
- **Opacity**: 全モード共通のブレンド強度（0〜100%）
- 8 bpc / 16 bpc 対応

> **Add** と **Negative Add** はスケール方式（Opacityが塗り色の寄与量をスケール）。  
> その他のモードはlerp方式（OpacityがsrcとブレンドResult間を補間）。

## 動作環境

- After Effects 2024 以降
- Windows（macOS対応予定）

## インストール

`KO_SimpleFill.aex` を After Effects のプラグインフォルダにコピーします：

```
C:\Program Files\Adobe\Adobe After Effects <バージョン>\Support Files\Plug-ins\
```

エフェクトメニューの **yama-ko.net > KO SimpleFill** から適用できます。

## ビルド方法

### 必要なもの

- Visual Studio 2022 以降（C++ デスクトップ開発）
- [After Effects SDK](https://developer.adobe.com/after-effects/)（25.6 以降）

### 環境変数の設定

ソリューションを開く前に以下のユーザー環境変数を設定してください：

| 変数名 | 内容 | 例 |
|---|---|---|
| `AE_SDK_ROOT` | AE SDK の `Examples` フォルダのパス | `C:\AE_SDK\...\Examples` |
| `AE_PLUGIN_BUILD_DIR` | `.aex` の出力先フォルダ | `C:\...\Plug-ins\Effects` |

### ビルド手順

1. `Win\KO_SimpleFill.sln` を Visual Studio で開く
2. 構成を **Release | x64** に設定
3. Ctrl+Shift+B でビルド

## ライセンス

Apache License 2.0 — [LICENSE](LICENSE) を参照
