# 六牌之外的成果 / Beyond six-card Poker

以下区分可借鉴的基础代码和仍需按模式研究的消费者。它们不是通用SDK。

The following distinguishes reusable infrastructure from mode-specific consumers. These examples are not a universal modding SDK.

| 部分 / Area | 代码 / Code | 可借鉴内容与边界 / Value and boundary |
|---|---|---|
| 启动 / Startup | `src/launcher/`, `src/helper/` | 文件版本校验、加载和安装检查；新模块要替换安装集合。 / Pinning, loading and checks; replace the module/installer set for new work. |
| 资料身份 / Profiles | `src/launcher/profile_isolation.h` | 改版独立资料目录；字符串地址和签名配置行为属于固定构建。 / Separate mod data; string sites and signed-config behavior are build-specific. |
| 模式隔离 / Scoping | `src/game/mode_scope.cpp` | 在共享工厂边界切换规则；当前仅识别Poker及三个原版任务。 / Switch rules at a shared factory boundary; current selectors cover Poker and its three quests. |
| 界面动画 / UI and animation | `src/game/pokerboard_face_draw.cpp`, `native_*_bytes.inc` | 原版资源、工具提示、曲线、图层及特效生命周期；坐标和字段依赖Poker。 / Native assets, hover, curves, layering and effect ownership; consumers remain Poker-specific. |
| 统计图 / Charts | `src/game/pokerendlevel_stats.cpp` | 保留原版样式而替换数据；仍绑定牌局结算入口。 / Replace data while retaining chart style; the entry point is Poker-specific. |
| 持久化 / Persistence | `src/game/game_stats_record.h`, `stats_pair_io.h`, `game_stats_persistence.cpp` | 主档配对、版本、成功后提交；108字节格式和读写入口属于当前玩法。 / Pairing, versions, post-success commits; schema and native seams belong to this implementation. |
| 保存边界 / Save timing | `src/game/deferred_save.cpp` | 在玩法稳定后保存；新模式须重新研究暂停、关闭和事务完成条件。 / Save at stable boundaries; investigate pause, close and completion semantics for each new mode. |
| 验证 / Tests | `tests/` | 模式隔离和实际原版指令测试；两项检查不代表覆盖全部模式和Hook。 / Scope/native-instruction checks; two tests do not cover every mode or hook. |

表内省略路径的游戏侧文件位于`src/game/`。`native_*`包含派生原版字节，来源和生成说明仍待补充，不能作为原创高层实现介绍。

Unqualified game-side filenames are under `src/game/`. Native-derived byte materials still require provenance/generation notes and should not be presented as original high-level source.

## 如何用在其他模式 / Applying this to other modes

1. 查明目标模式的对象、入口、有效动作和生命周期。 / Establish objects, entry points, accepted actions and lifecycle.
2. 复用启动、版本校验和独立身份方法，替换对应入口。 / Adapt startup, pins and profile identity, replacing mode-specific seams.
3. 根据需要增加扩展侧数据，避免让未修改的原版数组越界。 / Add sidecar state without enlarging unmodified native arrays.
4. 接入该模式的显示、结算和保存边界，分别验证。 / Integrate and verify display, settlement and persistence boundaries separately.

新增模式、重写棋盘规则、完全脱离原版运行仍需要额外研究。现有Poker证据不能证明其他模式已完整逆向。这些是可行开发方向，不是已实现功能。

New modes, replacement board rules and standalone execution require more research. Poker evidence does not establish complete understanding of other modes; these are development directions, not implemented features.

## GPT辅助 / GPT assistance

代码、研究、测试与说明的整理使用了GPT/Codex辅助，维护者及玩家提供玩法决策和人工反馈。公开材料用于让他人检查、复现和修正；AI分析不是原版开发者文档。

GPT/Codex assisted the code, research, tests and documentation. The maintainer and players supplied gameplay decisions and manual feedback. The material enables inspection, reproduction and correction; AI analysis is not original developer documentation.
