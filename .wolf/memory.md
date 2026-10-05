# Memory

> Chronological action log. Hooks and AI append to this file automatically.
> Old sessions are consolidated by the daemon weekly.

| 21:04 | ItemContainer 增加 ItemTable + FindItemByID/AddItemByID（按 ItemID 查表、按堆叠规则放入） | Source/Wildforge/Public/ItemSystem/Components/ItemContainer.h, Private/.../ItemContainer.cpp | 完成，UHT 通过 | ~6k |
| 21:04 | APlayerCharacter 补齐所有服务器权威函数的 Server RPC（AddItem/RemoveAll/Organize/Clear/Resize/Initialize） | Source/Wildforge/Public/Character/Player/PlayerCharacter.h, Private/.../PlayerCharacter.cpp | 完成，UHT 通过 | ~3k |
| 21:04 | 尝试构建 WildforgeEditor | - | 被 Live Coding 阻塞（编辑器 PID 34068 在运行） | ~1k |
| 21:16 | 新增 UItemDatabaseSubsystem（持有 DT_Items + int 键懒缓存），ItemContainer 去掉 ItemTable/FindItemByID，改 AddItemStack | Public/Private/ItemSystem/Database/ItemDatabaseSubsystem.h/.cpp, ItemContainer.h/.cpp | 完成 | ~9k |
| 21:16 | Server RPC 从 APlayerCharacter 下沉到 UItemContainer 组件（所有拥有者自动继承） | ItemContainer.h/.cpp, PlayerCharacter.h/.cpp | 完成 | ~5k |
| 21:16 | 构建 WildforgeEditor | - | 成功（UHT 8 files，9 actions，链接通过） | ~2k |
| 21:22 | 收紧 RPC：移除 Server_InitializeContainer/Server_ResizeContainer，保留 Server_ClearContainer 供客户端 | ItemContainer.h/.cpp | 构建成功 | ~3k |
| 21:26 | DataTable 加载改为配置驱动（UCLASS(Config=Game) + UPROPERTY(Config) FSoftObjectPath），默认值写入 DefaultGame.ini | ItemDatabaseSubsystem.h/.cpp, Config/DefaultGame.ini | 构建成功 | ~4k |
| 21:36 | 配置改由 UDeveloperSettings 承载：新增 UItemSystemSettings（Project Settings 可编辑），子系统改读 GetDefault<>；ini 段改名为 ItemSystemSettings；Build.cs 显式加 DeveloperSettings 依赖 | ItemSystemSettings.h/.cpp(新), ItemDatabaseSubsystem.h/.cpp, Config/DefaultGame.ini, Wildforge.Build.cs | 构建成功（8 actions） | ~5k |
