#pragma once
// Log lines and the player message in three languages ("en", "ja", "zh"; config.json "language").
// Placeholders are {} as in fmt; every language keeps the same order of values.

#include <array>
#include <string>
#include <string_view>

namespace invlink {

enum class Msg {
    ConfigCreated,
    ConfigBroken,
    ServerEmpty,
    StoreUnusable,
    Sharing,
    StillOwned,
    OwnerStopped,
    BackedUp,
    Loaded,
    StartedHere,
    ReadFailed,
    SaveFailed,
    Kick,
    Count_
};

enum class Lang { En, Ja, Zh };

inline Lang parseLang(std::string_view s) {
    if (s == "ja") return Lang::Ja;
    if (s == "zh" || s == "zh-CN" || s == "zh_CN") return Lang::Zh;
    return Lang::En;
}

inline std::string_view text(Lang lang, Msg msg) {
    using Row = std::array<std::string_view, 3>; // en, ja, zh
    static constexpr std::array<Row, static_cast<std::size_t>(Msg::Count_)> table{{
        // ConfigCreated
        {"Created config.json. Nothing is shared until \"server\" names this server (for example s1).",
         "config.json を作りました。\"server\" にこのサーバーの名前（s1 など）を書くまで共有しません。",
         "已创建 config.json。在 \"server\" 中填写本服务器的名称（例如 s1）之前不会共享。"},
        // ConfigBroken
        {"Cannot read config.json, nothing is shared: {}",
         "config.json を読めないため、共有しません: {}",
         "无法读取 config.json，不进行共享：{}"},
        // ServerEmpty
        {"\"server\" in config.json is empty, nothing is shared.",
         "config.json の \"server\" が空なので、共有しません。",
         "config.json 中的 \"server\" 为空，不进行共享。"},
        // StoreUnusable
        {"Cannot use the shared folder, nothing is shared: {}",
         "共有フォルダを使えないため、共有しません: {}",
         "无法使用共享文件夹，不进行共享：{}"},
        // Sharing
        {"Sharing as server {}: {}",
         "サーバー {} として共有します: {}",
         "以服务器 {} 的身份共享：{}"},
        // StillOwned
        {"{}: the items are still held by {} (not saved there yet); the player is asked to join again.",
         "{}: 持ち物はまだ {} にあります（保存が終わっていない）。入り直してもらいます。",
         "{}：物品仍由 {} 持有（尚未保存完毕），请玩家重新进入。"},
        // OwnerStopped
        {"{}: {} stopped without releasing the items; starting from its last save (version {}).",
         "{}: {} が持ち物を手放さないまま止まったため、最後の保存（版 {}）から始めます。",
         "{}：{} 在未释放物品的情况下停止，将从其最后一次保存（版本 {}）开始。"},
        // BackedUp
        {"{}: kept this server's own items in backup/{}/ before sharing.",
         "{}: このサーバーにあった持ち物を backup/{}/ に控えました。",
         "{}：已将本服务器原有的物品备份到 backup/{}/。"},
        // Loaded
        {"{}: loaded the shared items (version {}, saved by {}).",
         "{}: 共有の持ち物（版 {}、{} が保存）を読み込みました。",
         "{}：已读取共享物品（版本 {}，由 {} 保存）。"},
        // StartedHere
        {"{}: no shared record yet; starting from this server's items.",
         "{}: 共有の記録が無いため、このサーバーの持ち物から始めます。",
         "{}：尚无共享记录，从本服务器的物品开始。"},
        // ReadFailed
        {"{}: cannot read the shared items ({}); the player cannot join, to protect the items.",
         "{}: 共有の持ち物を読めません（{}）。持ち物を守るため、この人は入れません。",
         "{}：无法读取共享物品（{}）。为保护物品，该玩家无法进入。"},
        // SaveFailed
        {"{}: cannot save the shared items: {}",
         "{}: 共有の持ち物を保存できません: {}",
         "{}：无法保存共享物品：{}"},
        // Kick
        {"Your items are still being saved on the server you left. Please wait a few seconds and join again.",
         "前のサーバーで持ち物の保存が終わっていません。数秒待ってから、もう一度入ってください。",
         "你的物品仍在上一个服务器保存中。请等待几秒后重新进入。"},
    }};
    return table[static_cast<std::size_t>(msg)][static_cast<std::size_t>(lang)];
}

} // namespace invlink
