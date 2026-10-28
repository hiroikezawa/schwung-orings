# oRings

（モジュール ID: `orings`。「oRings」は開発用の仮の名前です）

Schwung / Ableton Move 用の Sound Generator です。中身は Mutable Instruments の
ユーロラック・モジュール **Rings**（レゾネーター）の**非公式移植**で、
Emilie Gillet 氏が MIT ライセンスで公開している公式ファームウェアの DSP ソースコードを
そのまま同梱して動かしています。共鳴体を新しく作り直したり、簡略化したりはしていません。

- Rings の 3 つの標準モデル（Modal Resonator / Sympathetic Strings /
  Modulated・Inharmonic String）と、公式の隠しモード 4 種
  （**2-op FM**、**Western Chords**、**Karplusverb**、
  **Disastrous Peace** ＝ オルガン／ストリングマシン、エフェクト 6 種類）を、
  MODEL ノブで直接選べます。隠し操作は要りません。
- 音を作る部分（`rings::Part`、`rings::StringSynthPart`、`rings::Strummer`）は、
  実機のファームウェア `rings.cc` と**同じ呼び出し方・同じ順番**で動かしています。
  移植で置き換えたのは、ポット・CV／トリガー入力・出力ジャックといった
  **ハードウェアの部分だけ**です。
- 実機と同じ **48 kHz・24 サンプルブロック**で DSP を動かし、Move の 44.1 kHz に
  リサンプルします。
- テストで、このモジュールの出力が、公式 DSP を実機と同じ配線で直接駆動したときの
  出力と **1 サンプル単位で完全一致**することを、全 12 モード × 1〜4 ボイスで
  確認しています（「検証」）。

| | |
|---|---|
| 残したもの | 全モデルの共鳴体（モーダル 64 モード、弦 8 本、FM ボイス、ストリングシンセ 12 ボイス）、内蔵エキサイター（モデルごとのパルス／ノイズバースト）、STRUM 処理、ボイス割り当て（ラウンドロビン、3 ボイス時のピンポン）、ODD / EVEN の 2 出力、リミッター、リバーブ／コーラス／アンサンブル／フォルマント |
| 省いたもの | 外部オーディオ入力（IN ジャック）と自動ストラム検出、各ノブの CV 入力とアッテヌバーター、キャリブレーション、LED とボタン操作 |
| 加えたもの | MIDI 演奏（ノート = V/OCT ＋ STRUM）、ベロシティによる打撃の強さ、ピッチベンド、微調整、和音を 1 音ずつ鳴らす間隔、出力の振り分け、音量 |

---

## 使い方（はじめての操作）

1. Move のトラックの Schwung スロットで、Sound Generator に **oRings** を選びます。
2. パッドを叩くと、そのノートの高さで共鳴体が「打たれて」鳴り、自然に減衰します。
3. ノブ 6（MODEL）で音のモデルを切り替えます。ノブ 2〜5 の名前は、
   モデルに合わせて変わります（例: FM では RATIO / INDEX / DECAY / FDBK）。
4. ノブ 4（DAMP）で余韻の長さ、ノブ 3（BRIGHT）で音の明るさが変わります。
5. ジョグを回すと 2 ページ目（Advanced）に移ります。

**Rings には ADSR がありません。** パッドを離しても音は止まりません。
余韻の長さは DAMPING（とモデル自身の物理）で決まります。
これは実機と同じ振る舞いです。

---

## ノブ

### 1 ページ目（Main）

| ノブ | 表示 | 内容 |
|---|---|---|
| 1 | FREQ | 移調 -24〜+24 半音。MIDI ノートの音程はそのまま、全体を上下させます |
| 2 | STRUCT など | 倍音・音程の構造（モデルにより名前が変わる。下表） |
| 3 | BRIGHT など | 明るさ（励振と共鳴の高域） |
| 4 | DAMP など | 減衰時間 |
| 5 | POS など | 打つ位置（倍音の出方） |
| 6 | MODEL | 12 モード（下表） |
| 7 | POLY | ボイス数 1 / 2 / 3 / 4 |
| 8 | LEVEL | 音量 |

### MODEL と、ノブ 2〜5 の名前

| MODEL | 内容 | 2 | 3 | 4 | 5 |
|---|---|---|---|---|---|
| MODAL | モーダル共鳴体（叩いた板や鐘のような共鳴） | STRUCT | BRIGHT | DAMP | POS |
| SYMPA | 共鳴弦（主弦＋共鳴して鳴る弦） | STRUCT | BRIGHT | DAMP | POS |
| STRNG | 変調・非調和の弦（STRUCT で弦の硬さ） | STRUCT | BRIGHT | DAMP | POS |
| KVERB | Karplusverb（弦＋リバーブ） | STRUCT | BRIGHT | DCY+RV | POS |
| FM | 2 オペレーター FM | RATIO | INDEX | DECAY | FDBK |
| CHORD | Western Chords（共鳴弦が和音に固定） | CHORD | BRIGHT | DAMP | POS |
| DP-FM | Disastrous Peace＋フォルマント | CHORD | REGIST | ENV | VOWEL |
| DP-CH | Disastrous Peace＋コーラス | CHORD | REGIST | ENV | CHORUS |
| DP-RV | Disastrous Peace＋リバーブ | CHORD | REGIST | ENV | REVERB |
| DP-F2 | Disastrous Peace＋フォルマント 2 | CHORD | REGIST | ENV | VOWEL |
| DP-EN | Disastrous Peace＋アンサンブル | CHORD | REGIST | ENV | ENSMBL |
| DP-R2 | Disastrous Peace＋リバーブ 2 | CHORD | REGIST | ENV | REVERB |

- 名前が変わっても、中で動かしている値は同じ 4 つ（STRUCTURE / BRIGHTNESS /
  DAMPING / POSITION）です。公式 DSP がモードごとに割り当てている意味を、
  そのまま画面に表示しています。
- **FM**: RATIO = モジュレーターの周波数比（公式の量子化テーブル）、
  INDEX = FM の深さ、DECAY = 内蔵エンベロープの長さ（最大付近ではドローン）、
  FDBK = フィードバック（中央が 0、左右で 2 種類のフィードバック）。
- **CHORD**（Western Chords と Disastrous Peace）: ノブ 2 で 11 種類の和音
  （OCT, 5, sus4, m, m7, m9, m11, 69, M9, M7, M）を選びます。
- **Disastrous Peace** は、実機で隠し操作をすると現れるオルガン／ストリングマシンです。
  REGIST = オルガンのレジストレーション（倍音の組み合わせ）、
  ENV = 回すほど減衰が長くなり、右半分ではアタックも遅くなり、右端でドローン、
  ノブ 5 = エフェクトの量（フォルマントでは母音）。
  発音・和音・エンベロープ・エフェクトはすべて公式の `StringSynthPart` です。
- **KVERB**: DCY+RV は弦の減衰とリバーブの量・長さを同時に動かします。
  POS は打つ位置と、ステレオの広がりも動かします。

### 2 ページ目（Advanced）

| ノブ | 表示 | 内容 |
|---|---|---|
| 1 | FINE | 微調整 ±100 セント |
| 2 | EXCITE | 打撃（内部エキサイター）の強さ 0〜200 %。100 % が実機と同じ |
| 3 | SPREAD | 和音を弾いたとき、1 音ずつ鳴らす間隔 10〜200 ms |
| 4 | VELO | ベロシティの効き 0〜100 %（0 % = 常に最大の強さ） |
| 5 | BEND | ピッチベンドの幅 0〜24 半音 |
| 6 | OUTPUT | STEREO（ODD = 左、EVEN = 右）/ SUM（ODD＋EVEN のモノラル）/ ODD / EVEN |
| 7 | GAIN | -24〜+12 dB |
| 8 | RESET | クリックで全ボイスを無音にする（ノブを回しても動きません） |

**SPREAD について。** 実機の Rings は、ストラム（トリガー）が 10 ms 以内に
続くと 2 回目以降を無視します（`Strummer` の仕様）。和音を同時に弾くと
1 音しか鳴らないことになるので、このモジュールではノートを順番待ちにして、
最低 10.5 ms ずつずらして 1 音ずつストラムします。SPREAD を上げると
ギターのストロークのような時間差になります。実機の
「自動ストラム検出」は外部オーディオ入力用の機能で、楽器として使う
このモジュールには入力がないため、代わりにこのノブを置いています。

**EXCITE と VELO について。** 実機の内部エキサイターは打撃の強さが一定です。
このモジュールでは、打撃の強さ（モーダルのパルス、弦のノイズバースト、
FM の内蔵エンベロープ、Disastrous Peace のエンベロープの高さ）に
「EXCITE × ベロシティ」を掛けています。強さ 1.0（EXCITE 100 %・ベロシティ 127）
のとき、出力は公式 DSP と 1 サンプル単位で同じです。

### ODD / EVEN

Rings には ODD と EVEN の 2 つの出力があります。

- **1 ボイス**: 1 つの共鳴体の 2 つのピックアップ（倍音の取り出し方が違う）。
- **2〜4 ボイス**: ボイスが交互に ODD と EVEN に振り分けられます。
  最初のノートは EVEN（右）、次は ODD（左）…と交互です（実機と同じ）。

OUTPUT が STEREO のとき、ODD が左、EVEN が右に出ます。左右に同じ音を
コピーしたものではありません。

---

## MIDI

| メッセージ | 動作 |
|---|---|
| ノートオン | 音程（V/OCT）を設定してストラム。同じノートでも毎回ストラム |
| ノートオフ | 何もしない（Rings にはゲートがない） |
| ベロシティ | 打撃の強さ（VELO と EXCITE） |
| ピッチベンド | 全体の音程（BEND の幅、実機の FM 入力と同じ平滑化） |
| CC 120（All Sound Off） | RESET と同じ |
| CC 123（All Notes Off） | 順番待ちのノートを捨てる |
| CC 121 | ピッチベンドを 0 に戻す |

MIDI チャンネル、スケール、移調は Schwung / Move 側の仕組み（スロットの受信
チャンネル、パッドのスケール）がそのまま使えます。

ボイスの割り当ては公式のまま、ストラムごとに次のボイスへ（3 ボイスのときは
実機と同じ 1,0,2,1,0,2… のパターン）です。

---

## サンプルレートと音程

Rings の DSP は 48 kHz（`rings/dsp/dsp.h` の `kSampleRate`）を前提に、
音程・エンベロープ・フィルター・ディレイ・リバーブの時間をすべて計算しています。
そこでこのモジュールは DSP を実機どおり 48 kHz・24 サンプル単位で動かし、
64 タップのポリフェーズ FIR で 44.1 kHz に変換しています（20 kHz まで平坦、
遅延 約 0.7 ms ＋ 最大 1 ブロック 0.5 ms）。音程の誤差は FM の正弦波で測って
C1〜C7 で 0.05 セント以内です。

弦モデル（STRNG）は C1〜C7 で最大 15 セント、Karplusverb は 10 セントほど
ずれます。これは公式の弦モデル自体の特性（減衰フィルターの遅れ）で、
移植による誤差ではありません。

---

## 設定の保存

全パラメーターは `state` として保存され、Schwung のスロット保存・
My Presets で保存／復元できます（RESET は保存されません）。

---

## 検証（`scripts/test.sh`）

- **上流との一致**: 同梱 DSP（パッチ込み）と、未改変の公式ソースを同じ入力
  （全 6 モデル＋6 FX × 1〜4 ボイス、ノブ・和音・FM・ストラムを動かす 460 万
  サンプル）で動かし、出力がバイト単位で一致。
- **ラッパーが何も足していないこと**: このモジュールの出力が、公式 DSP を
  `rings.cc` と同じ順番で手で駆動した出力と、全 12 モード × 4 ボイス数で
  ビット一致。
- 音程（C1〜C7）、ベンド・FINE・FREQ、同音連打（12 回 → 12 ストラム）、
  4 音同時の和音（4 ストラム、取りこぼし 0）、ノートオフで音が変わらないこと、
  1/2/3/4 ボイス、ODD/EVEN の振り分け、各ノブで音が変わること（全モデル）、
  Disastrous Peace の 6 エフェクトがそれぞれ違う音であること、出力モード、
  ベロシティ、状態の保存と復元、RESET、モデル切り替え時にクリックが出ないこと、
  10 分間のランダム演奏（全モデル、NaN・異常振幅なし）。ASan / UBSan 付き。
- プラグインとして dlopen して全モデルを鳴らすスモークテスト。
- Schwung 本体のページエンジン（実機からコピーした `shared/`）で、
  全 12 モデルのノブの並びと名前、MODEL ノブを回したときの再配置を確認。

---

## ビルドとインストール

```bash
scripts/test.sh                                # 全テスト + Move 用ビルド
scripts/build.sh arm64                         # dist/orings-module.tar.gz
python3 scripts/install.py http://192.168.1.42:7700
```

インストール後は Move を再起動してください。

このリポジトリは、同じ Rings DSP（`src/rings/mi/`）から Audio FX 版の
**oRings FX**（`orings-fx`、入力音で共鳴体を鳴らすエフェクト）も作ります。
`scripts/build.sh arm64` で `dist/orings-fx-module.tar.gz` もでき、
`--module orings-fx` を付けて同じ `install.py` でインストールします。
説明は `fx/README.md` にあります。

同梱の DSP には公式ソースに対するパッチが 2 つあります（`patches/`）。
`0001` は上記の打撃の強さ、`0002` は stmlib の `Interpolate` がテーブルの
最後の要素ちょうどで 1 つ先を読んでいた不具合（DAMP を最大 1.0 にしたときに
起きる。読んだ値には重み 0 が掛かるので音は変わらない）の修正です。

CPU 負荷（Move 実機）: `scripts/build_bench.sh` でベンチを作り、
`scripts/bench_move.sh ableton@<Move の IP> -i <鍵>` で測れます。

---

## ライセンス

MIT（`LICENSE`）。同梱している Rings の DSP と stmlib は Emilie Gillet 氏の
MIT ライセンスのコードです（`THIRD_PARTY_LICENSES.md`）。

このモジュールは非公式の移植で、Mutable Instruments とは関係ありません。
Mutable Instruments は登録商標です。
