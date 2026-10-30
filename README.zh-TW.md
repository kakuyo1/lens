<p align="center"><img src="docs/readme/brand/lens-mark.png" width="92" alt="Lens"></p>

<h1 align="center">Lens</h1>

<h3 align="center">用英文，讀懂英文。</h3>

<p align="center">
  Lens 是一款常駐 Windows 系統匣的英文閱讀助手。選取一個單字、一個名字或一個句子，它會在文字旁用更清楚的英文解釋——不切頁，不開字典，也不把英文先翻譯成中文。沒有取詞的時候，桌面寵物會把到期的詞舉回來問你。
</p>

<p align="center">
  <a href="https://github.com/kakuyo1/lens/releases/latest"><strong>下載 Windows 版</strong></a> ·
  <a href="#先看一眼">看它怎麼工作</a> ·
  <a href="https://github.com/kakuyo1/lens/releases">全部版本</a>
</p>

<p align="center">
  <a href="https://github.com/kakuyo1/lens/releases/latest"><img alt="release" src="https://img.shields.io/github/v/release/kakuyo1/lens?label=release&color=2f9e6e"></a>
  <a href="#下載"><img alt="Windows 10/11 x64" src="https://img.shields.io/badge/Windows-10%20%7C%2011%20x64-1a1a1f"></a>
  <a href="LICENSE"><img alt="MIT License" src="https://img.shields.io/github/license/kakuyo1/lens?color=2f9e6e"></a>
  <a href="#語言"><img alt="英文、中文、繁體中文、西班牙語、日語" src="https://img.shields.io/badge/languages-EN%20%7C%20ZH%20%7C%20ZH--HANT%20%7C%20ES%20%7C%20JA-2f9e6e"></a>
</p>

<p align="center">
  <img alt="C++20" src="https://img.shields.io/badge/C%2B%2B-20-00599C?logo=cplusplus&logoColor=white">
  <img alt="Qt 6 / QML" src="https://img.shields.io/badge/Qt-6.2%2B%20%2F%20QML-41CD52?logo=qt&logoColor=white">
  <img alt="CMake 3.21+" src="https://img.shields.io/badge/CMake-3.21%2B-064F8C?logo=cmake&logoColor=white">
</p>

<p align="center"><sub><a href="README.en.md">English</a> · <a href="README.md">簡體中文</a> · 繁體中文 · <a href="README.ja.md">日本語</a> · <a href="README.es.md">Español</a></sub></p>

<p align="center">
  <picture>
    <source media="(prefers-color-scheme: dark)" srcset="docs/readme/hero-dark.png">
    <img src="docs/readme/hero-light.png" width="100%" alt="Lens 在英文文章旁用英文解釋 serendipity 這個詞">
  </picture>
</p>

<p align="center"><sub>解釋預設使用英文；按住卡片上的地球就能看到母語釋義，鬆手即收。永久切換解釋語言在設定裡。</sub></p>

## 為什麼是 Lens

大多數查詞工具會把你帶離頁面，再給一個中文答案。你讀完了一段，卻沒有用英文讀它。Lens 的選擇相反：提高英文閱讀最有效的方式，是留在英文裡。

**英文進，清楚的英文出。**一條你能直接讀懂的英文釋義、音標；需要時再加上詞源和例句。翻譯仍然在，只是它不是預設動作。

**能聽見的詞。**音標旁有一枚喇叭：點它，或點音標本身，就會讀一次詞頭。句子、名字和譯文不會出聲。

**答案留在文字旁。**Lens 常駐系統匣，沒有主視窗。氣泡出現在選取範圍旁邊，游標停留在上面就會一直留著；你繼續往下讀，它就退場。

**安靜的記憶。**把詞標成 “已會” 或 “新詞”，Lens 替你記著。沒有連續打卡，也沒有課表。

**會問你的寵物。**到期的詞會在寵物腳邊立起一塊提問牌：答對，下一次隔得更久；答錯，就提前回來。寵物只收事件類型，讀不到螢幕、選取或釋義。預設關閉，在設定的 “擴充功能” 裡開啟。

**能走進去的釋義。**英文釋義裡超出你等級的詞會留下點線；點開它，同一張卡片向下走一層，並留一條路徑列帶你回來。快取、預算與判定規則跟著每一步走。

## 先看一眼

<p align="center"><img src="docs/readme/english-first.gif" width="600" alt="按住卡片上的地球，在英文釋義下看到中文意思"></p>

答案仍然是英文。按住地球看母語，鬆手，卡片又回到英文。

<p align="center"><img src="docs/readme/feature-tour.gif" width="600" alt="在 crane 的卡片上點 “圖片”，照片載入；點 “不是這個義項？”，卡片原地換成起重機的釋義和照片"></p>

單字卡上點 “圖片”，照片就載入；點 “不是這個義項？”，卡片原地換到另一個義項，照片跟著變。

## 英文優先，需要時再翻譯

解釋預設使用英文。中文、日文或西班牙文的選取範圍都會用英文回答。解釋語言——英文、中文、繁體中文、西班牙文或日文——是設定裡的一項，不再放在卡片上：改一次，之後每張卡都跟著走，已有快取的詞直接取快取。

動作列提供翻譯、解釋和複製文字。Lens 圍繞的是 “解釋”：用你已經認識的英文，說明這個詞在這裡做什麼，而不是換成一個你可以直接跳過、不再需要讀英文的中文詞。翻譯是例外：中文、日文或西班牙文的選取範圍會譯成英文；英文選取範圍則按解釋語言回答——預設英文，除非你改。

卡片上留下的語言控制只有那枚地球：按住半秒，英文釋義下方浮出母語的意思，鬆手即收。母語本身是設定裡的一項，預設中文。解釋時已經取回那門語言的詞，按住不花錢；其餘的詞每個工作階段問一次。

## 讀出來

每張單字卡的音標旁都有一枚喇叭：點喇叭或點音標本身，讀一次詞頭。卡片上的其他內容不會出聲——釋義、句子、名字都不讀。

預設先由 Windows 的系統音色發聲，口音依你選的美式（預設）或英式。系統沒有該口音的音色時，依你在設定 › 閱讀學習裡選的備援走：線上音訊（預設有道，也可選 Google 翻譯）、下載一次的本地模型（Kokoro 約 180 MB，或 LJSpeech 約 41 MB），或者不發音。這一檔由你明確選擇——Lens 不會在線上與本地之間悄悄切換。自動播放預設關閉；開啟後，取詞彈出的那張卡出現即讀一次，同一個詞不重複。線上音訊快取在 `%APPDATA%\Lens\audio`，上限 50 MB，不計入每日預算。

## 無法選取的文字

按 `Ctrl+Alt+S`（可在設定中修改），框選 PDF、圖片或影片畫面中的一塊區域，Lens 會在本機用 OCR 讀取。隨包的 Tesseract 是自動引擎；讀者可以自行安裝 RapidOCR-json，明確選擇它，並在一次信任檢查後指向 ASCII 模型目錄。線上 OCR 是可選項，每次上傳前都會詢問，可指向百度 OCR、騰訊雲 OCR、阿里雲 OCR、Google Cloud Vision 或 OCR.space。自動掃描仍是實驗功能，尚在開發：它只監看你加入白名單的應用程式，結果尚不可靠。

## 五種動作列樣式

<p align="center"><img src="docs/readme/selection-styles.gif" width="800" alt="經典長條、頁緣標籤、手牌、吊牌和扇面五種動作列樣式"></p>

在設定 › 取詞與彈出視窗裡選擇：經典長條、頁緣標籤、手牌、吊牌或扇面。三項動作和通道規則完全不變，複製抽屜依它展開的那副外形畫。

<p align="center"><img src="docs/readme/selection-drawer.png" width="800" alt="選取一個句子時，複製抽屜列出糾正語法與拆解"></p>

複製文字是一張抽屜，不是單一動作：指標停在上面，只有這張選取範圍能做的事會出來——單字可糾正拼寫，句子可糾正語法，80 詞以內的英文句子還可拆解。糾正與拆解都不入快取，各自按一次請求計入每日預算。

<p align="center"><img src="docs/readme/breakdown.png" width="800" alt="拆解的作答：句子按四類成分染色，下方配一行圖例"></p>

拆解把句子的四類成分各染一色——主語、述語、表語、狀語——下面配一行圖例；糾正只給改動過的詞畫底線。

## 一個安靜的學習迴圈

<p align="center"><img src="docs/readme/learning-loop.gif" width="900" alt="小幽靈腳邊的提問牌，以及單字年度紀錄"></p>

已會和新詞在同一張詞表裡。把指標停在某一行上，可以重讀 Lens 已經存下的解釋，不會發出請求。單字到期時，寵物腳邊會立起它自己的提問牌；回答 “忘了” 或 “記得”，它會按 10 分鐘到 60 天的間隔再次被問起。年度紀錄把閱讀留下的痕跡畫成一格一格的綠色，而不是一條連續打卡。

## 會把詞舉回來問你的寵物

<p align="center"><img src="docs/readme/pets.gif" width="900" alt="Lens 的七隻像素寵物"></p>

Lens 隨包提供七隻像素寵物：短耳貓、貓頭鷹、麻糬、小幽靈、小水母、小雲和小狗。它們預設關閉，可在設定的 “擴充功能” 裡開啟。每隻都有自己的九件配件和六個場景——四個專屬、兩個共享；一條圍巾或一副眼鏡會改變它更常去哪個場景，大小與穿搭按寵物各記一份，視窗位置仍只屬於你。它們會閒逛、在螢幕邊緣躲起來、注意到指標，也能被拎起來拋出去。它們只接收事件類型——從不讀取你的選取範圍、解釋或螢幕上的文字——寵物包也只含資料。

小幽靈的六個：午夜座鐘、南瓜燈、鏡中無影與幽靈茶會是它自己的，敲電腦和泡澡與其他寵物共享——每個場景都是一段自己的小動畫。南瓜燈播起來是這樣：

<p align="center"><img src="docs/readme/pet-lantern.gif" width="420" alt="小幽靈的南瓜燈場景，正在播放"></p>

在設定的 “擴充功能” 裡把指標停到一隻寵物上，它會舉起自己的明信片——按部位各穿一件，站在只屬於它的風景裡，貼著郵票、蓋著郵戳。

<p align="center"><img src="docs/readme/pet-postcards.gif" width="700" alt="七隻寵物的明信片，逐一亮相"></p>

## 還有這些

- **自備模型。**DeepSeek、OpenAI、Gemini、OpenRouter、xAI、Moonshot、智譜、通義千問，或任何相容 OpenAI 的自訂端點。
- **看得見的花費。**依 token 計算每次請求的費用；每日預算到頂時，暫停取詞、截圖和請求。
- **想同步時再同步。**已會/新詞標記、查詞歷史、統計和選定設定用你的通行碼加密，存放在你自己 GitHub 帳號的 secret gist 裡；API key 始終留在本機。
- **詞表進得來也出得去。**匯入或匯出 `.txt` 和 `.json`；刪除單字需要一次確認。
- **該給圖時給圖。**模型判斷某條義項用照片更好說明時，卡片可以展開三張 Wikimedia Commons 圖片；圖片快取在本機，“關於” 頁顯示快取大小並可原地清除。
- **依你的習慣。**五種動作列樣式；晴朗白晝、靜謐暗夜、霧面玻璃、流光玻璃和自訂主題；以及英文字型與 12–18 px 氣泡字級。
- **五種語言。**介面和解釋各自提供英文、中文、繁體中文、西班牙語和日語。繁體中文、西班牙語與日語的翻譯由機器生成，尚未經母語人士審校。

## 主題

<p align="center"><img src="docs/readme/themes.png" width="900" alt="晴朗白晝、靜謐暗夜、霧面玻璃和流光玻璃"></p>

晴朗白晝和靜謐暗夜是純色主題。霧面玻璃和流光玻璃把每張卡片畫在模糊的桌面之上；玻璃主題開啟時，Lens 會把自己排除在截圖和螢幕分享之外。自訂配色會按 WCAG AA 檢查對比度。動畫開關跟隨 Windows 的動畫效果設定。

## 下載

從[發行頁](https://github.com/kakuyo1/lens/releases/latest)下載 `Lens-<版本>-setup.exe`，或同一版本裡的 `Lens-<版本>-portable.zip`。安裝版以使用者為單位安裝，Windows 不會要求系統管理員權限；免安裝版解壓後即可執行。

**Windows 10/11 x64。**你需要一個支援服務商的 API key。Lens 不內建釋義字典；它包含一份約 168,000 個英文單字的詞表，只用來判斷候選詞是不是真詞。

<details>
<summary>Windows 可能會先攔一下</summary>

Lens 沒有程式碼簽章，SmartScreen 會攔截它不認識的程式。點 **“更多資訊”**，再點 **“仍要執行”**。這是缺少簽章，不是檔案損毀。想核對下載是否完整，可以把檔案的 SHA-256 和發行版裡的 `SHA256SUMS` 對應行比一下；這隻能證明檔案在傳輸中沒有被改動，不能證明發行者是誰。

</details>

## 隱私

只有你選取或擷取到的文字會傳送給你設定的服務商，傳送前會在本機遮蓋 URL、電子郵件位址和六位及以上的數字字串。OCR 預設在本機執行；線上 OCR 需要你逐次確認，確認卡會寫明這次裁剪送往哪個服務，實驗中的自動掃描從不使用它。喇叭優先以系統音色或已下載的本地模型在本機朗讀；備援到線上音訊時，離開本機的只有那個詞，錄音快取在 `%APPDATA%\Lens\audio`。API key、OCR 憑據和學習資料儲存在 `%APPDATA%\Lens\settings.json`。開啟同步後，雲端副本用你的通行碼加密，存放在你自己的 GitHub gist 裡；金鑰始終只在本機。Lens 沒有自己的帳號，也沒有自己的伺服器。完整的資料邊界見 [SECURITY.md](SECURITY.md)。

## 常見問題

**Lens 是翻譯器嗎？**它可以翻譯，但它是為 “解釋” 做的。中文、日文或西班牙文的選取範圍返回英文。

**需要 API key 嗎？**需要。用支援服務商的 key；Lens 會顯示每次請求的費用，也可以設定每日上限。

**能離線用嗎？**OCR、詞表和已下載的本地音色在本機可用；解釋、線上 OCR 與線上音訊需要你設定的服務。

**什麼會離開我的電腦？**只有本機脫敏後的選取文字，加上線上 OCR 裡逐次確認的裁剪，以及備援到線上音訊時的那一個詞。見[隱私](#隱私)。

**為什麼 Windows 會警告？**安裝包沒有程式碼簽章；怎麼執行與核對檔案見[下載](#下載)一節的說明。

**有 macOS 或 Linux 版嗎？**目前只有 Windows。

## 語言

[English](README.en.md) · [簡體中文](README.md) · [繁體中文](README.zh-TW.md) · [日本語](README.ja.md) · [Español](README.es.md)

介面語言和解釋語言是兩個獨立設定，都可以選擇英文、中文、繁體中文、西班牙語和日語。

## 參與和許可

Lens 以 [MIT 授權](LICENSE)開源。歡迎提 issue 和 PR。如果 Lens 幫你多讀了一點英文，點一個 star 會讓更多讀者找到它。

<sub>介面圖片由 Lens 目前的設計原型渲染，實際建置遵循同一套版面配置與設計變數。</sub>
