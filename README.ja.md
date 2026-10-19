<p align="center"><img src="docs/readme/brand/lens-mark.png" width="92" alt="Lens"></p>

<h1 align="center">Lens</h1>

<h3 align="center">英語は、英語のまま読む。</h3>

<p align="center">
  Lens は Windows のタスクトレイに常駐する英語リーディングの相棒です。選んだ単語・名前・文を、文字のすぐ横でやさしい英語にして説明します。タブを切り替えず、辞書ウィンドウも開かず、翻訳という回り道もしません。
</p>

<p align="center">
  <a href="https://github.com/kakuyo1/lens/releases/latest"><strong>Windows 版をダウンロード</strong></a> ·
  <a href="#ちょっと見る">動きを見る</a> ·
  <a href="https://github.com/kakuyo1/lens/releases">すべてのリリース</a>
</p>

<p align="center">
  <a href="https://github.com/kakuyo1/lens/releases/latest"><img alt="release" src="https://img.shields.io/github/v/release/kakuyo1/lens?label=release&color=2f9e6e"></a>
  <a href="#ダウンロード"><img alt="Windows 10/11 x64" src="https://img.shields.io/badge/Windows-10%20%7C%2011%20x64-1a1a1f"></a>
  <a href="LICENSE"><img alt="MIT License" src="https://img.shields.io/github/license/kakuyo1/lens?color=2f9e6e"></a>
  <a href="#言語"><img alt="英語・中国語・スペイン語・日本語" src="https://img.shields.io/badge/languages-EN%20%7C%20ZH%20%7C%20ES%20%7C%20JA-2f9e6e"></a>
</p>

<p align="center">
  <img alt="C++20" src="https://img.shields.io/badge/C%2B%2B-20-00599C?logo=cplusplus&logoColor=white">
  <img alt="Qt 6 / QML" src="https://img.shields.io/badge/Qt-6.2%2B%20%2F%20QML-41CD52?logo=qt&logoColor=white">
  <img alt="CMake 3.21+" src="https://img.shields.io/badge/CMake-3.21%2B-064F8C?logo=cmake&logoColor=white">
</p>

<p align="center"><sub><a href="README.md">English</a> · <a href="README.zh-CN.md">简体中文</a> · 日本語 · <a href="README.es.md">Español</a></sub></p>

<p align="center">
  <picture>
    <source media="(prefers-color-scheme: dark)" srcset="docs/readme/hero-dark.png">
    <img src="docs/readme/hero-light.png" width="100%" alt="Lens が英文エッセイの横で serendipity を英語で説明している">
  </picture>
</p>

<p align="center"><sub>説明は既定で英語です。中国語・スペイン語・日本語にしたいときは、カードの言語ボタンから切り替えられます。</sub></p>

## なぜ Lens か

多くの辞書ツールは、ページから連れ出して母語で答えます。その段落を読み終えても、英語では読んでいない。Lens は逆を行きます。英語の読解力を上げる一番の近道は、英語の中にとどまることです。

**英語で入って、やさしい英語で出る。** 平易な英語の語義と IPA、必要なら語源と例文。翻訳も残っています。ただし既定ではありません。

**答えは文字の横に。** Lens はタスクトレイに住み、メインウィンドウを持ちません。バブルは選択範囲の横に開き、ポインターを乗せている間は留まり、読み進めれば消えます。

**静かな記憶。** 単語を「覚えた」「初見」で印をつけると、Lens が覚えます。デスクトップの相棒が後で出題することもあります。連続記録も学習計画もありません。

## ちょっと見る

<p align="center"><img src="docs/readme/english-first.gif" width="600" alt="バブル上で説明言語を英語から中国語に切り替える"></p>

言語の切り替えはカードの上にあります。既定は英語、中国語まであと 1 クリックです。

## まず英語。翻訳は求められたときだけ。

説明は既定で英語です。中国語の選択範囲はいつも英語で返します。中国語が欲しいときは、バブルの地球ボタンから説明言語を切り替えます。同じカードが答え直され、キャッシュがあればリクエストは送りません。

アクションバーには「翻訳」「解説」「テキストをコピー」があります。Lens の中心は「解説」です。知っている英語で、その単語がここで何をしているかを説明します。読み飛ばせる中国語に置き換えるのではありません。翻訳も説明言語に従います。既定は英語です。

## 使い方

<p align="center"><img src="docs/readme/selection-bar.png" width="800" alt="選択した文字の上にアクションバーが出る"></p>

1. どのアプリでも単語・名前・文を選択します。
2. 「解説」を選ぶと、選択範囲の上か下にバブルが出ます。
3. 「覚えた」「初見」で印をつけて、読み続けます。

テキストを選択できないときは `Ctrl+Alt+S`（設定で変更可）を押し、PDF・画像・動画の一部を囲みます。Lens がローカルの OCR で読み取ります。自動スキャンは許可リストに入れたアプリだけを見ます。

## 5 種類のアクションバー

<p align="center"><img src="docs/readme/selection-styles.gif" width="800" alt="クラシック、ページ端のタブ、3 枚のカード、吊り札、扇面の 5 種類"></p>

設定 › 取得とポップアップで選べます。クラシック、ページ端のタブ、3 枚のカード、吊り札、扇面。3 つの動作とチャンネルのルールは変わりません。

## 小さな学習ループ

<p align="center"><img src="docs/readme/learning-loop.png" width="900" alt="復習の札を掲げるデスクトップペットと単語の年間記録"></p>

「覚えた」と「初見」は一つの単語リストに並びます。行にポインターを乗せると、Lens が保存済みの説明を再表示します。リクエストは送りません。単語が復習時期になると、デスクトップの相棒が札を掲げます。「忘れた」「覚えていた」で答えると、10 分から 60 日までの間隔でまた出てきます。年間の記録は、連続記録ではなく、緑のマスで読書の跡を残します。

## 7 匹のデスクトップペット（任意）

<p align="center"><img src="docs/readme/pets.gif" width="900" alt="Lens の 7 匹のピクセルペット"></p>

Lens には 7 匹のピクセルペットが付属します。短耳猫、フクロウ、モチ、ゴースト、クラゲ、雲、犬。既定ではオフで、設定の「拡張」からオンにできます。イベントの種類だけを受け取り、選択範囲・解説・画面の文字は読みません。ペットパッケージはデータのみです。

## ほかにも

- **モデルを持ち込む。** DeepSeek、OpenAI、Gemini、OpenRouter、xAI、Moonshot、Zhipu、Qwen、OpenAI 互換のカスタムエンドポイント。
- **見えるコスト。** トークンからリクエストごとの費用を計算。1 日の予算に達すると、取得・キャプチャ・リクエストを止めます。
- **同期は必要なときだけ。** 「覚えた」「初見」、検索履歴、統計、選択した設定をパスフレーズで暗号化し、自分の GitHub アカウントの secret gist に保存します。API キーは端末に残ります。
- **単語リストの出し入れ。** `.txt` と `.json` をインポート/エクスポート。削除には確認が一度入ります。
- **好みに合わせて。** 5 種類のアクションバー。テーマはライト、ダーク、すりガラス、リキッドガラス、カスタム。英語フォントと 12–18 px のバブル文字。
- **4 言語。** インターフェースと説明はそれぞれ英語・中国語・スペイン語・日本語。スペイン語と日本語の翻訳は機械生成で、母語話者の校閲を受けていません。

## テーマ

<p align="center"><img src="docs/readme/themes.png" width="900" alt="ライト、ダーク、すりガラス、リキッドガラス"></p>

ライトとダークは単色です。すりガラスとリキッドガラスは、すべてのカードをぼかしたデスクトップの上に描き、有効な間は Lens 自身をスクリーンショットと画面共有から外します。カスタムでは色を選び、WCAG AA のコントラストを確認します。アニメーションは Windows のアニメーション設定に従います。

## ダウンロード

[リリースページ](https://github.com/kakuyo1/lens/releases/latest)から `Lens-<version>-setup.exe`、または同じリリースの `Lens-<version>-portable.zip` をダウンロードします。インストール版はユーザー単位なので、Windows は管理者権限を求めません。ポータブル版は任意のフォルダーから実行できます。

**Windows 10/11 x64。** 対応プロバイダーの API キーが必要です。Lens は定義辞書を同梱していません。約 168,000 語の英語リストは、候補が実在する単語かを確認するためだけに使います。

<details>
<summary>Windows が先に警告するかもしれません</summary>

Lens はコード署名されていないため、SmartScreen が知らないプログラムを止めます。**「詳細情報」**、次に **「実行」** を選んでください。警告は署名がないことについてで、ファイルが壊れているわけではありません。ダウンロードが完全かを確認するには、ファイルの SHA-256 をリリースの `SHA256SUMS` の該当行と比べます。これは転送中に改変されていないことを示すだけで、公開者が誰かまでは示しません。

</details>

## プライバシー

選択またはキャプチャしたテキストだけが、設定したプロバイダーに送られます。送信前に URL、メールアドレス、6 桁以上の数字列をローカルでマスクします。OCR はローカルで動作し、スクリーンショットはアップロードされません。API キーと学習データは `%APPDATA%\Lens\settings.json` にあります。同期を有効にすると、クラウドのコピーはパスフレーズで暗号化され、自分の GitHub gist に保存されます。キーは端末に残ります。Lens 自身のアカウントもサーバーもありません。詳しくは [SECURITY.md](SECURITY.md) を参照してください。

## FAQ

**Lens は翻訳ツールですか？** 翻訳もできますが、中心は解説です。説明は既定で英語です。翻訳が欲しいときは説明言語を切り替えてください。中国語の選択範囲は常に英語で返ります。

**API キーは必要ですか？** 必要です。対応プロバイダーのキーを使ってください。Lens はリクエストごとの費用を表示し、1 日の上限も設定できます。

**オフラインで使えますか？** OCR と単語リストはローカルです。説明には設定したプロバイダーが必要です。

**PC から何が出ていきますか？** 選択またはキャプチャした単語・名前・文だけが、マスク後に送られます。ページ全体やスクリーンショット全体はアップロードされません。

**Windows の警告はなぜですか？** インストーラーにコード署名がないためです。ファイル自体は完全です。警告は署名の欠如についてのものです。`SHA256SUMS` で確認できます。

**macOS や Linux 版はありますか？** 今は Windows のみです。

## 言語

[English](README.md) · [简体中文](README.zh-CN.md) · [日本語](README.ja.md) · [Español](README.es.md)

インターフェース言語と説明言語は別々の設定です。どちらも英語・中国語・スペイン語・日本語から選べます。

## コントリビュートとライセンス

Lens は [MIT ライセンス](LICENSE)のオープンソースです。issue と PR を歓迎します。Lens で英語を少し多く読めるようになったら、star が次の読者の助けになります。

<sub>インターフェース画像は Lens の現在のデザインプロトタイプから描画したものです。実際のビルドも同じレイアウトとトークンに従います。</sub>
