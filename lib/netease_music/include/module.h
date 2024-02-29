#ifndef MODULE_H
#define MODULE_H

#include <QVariantMap>

#if defined(_MSC_VER) || defined(WIN64) || defined(_WIN64) || defined(__WIN64__) || defined(WIN32) \
|| defined(_WIN32) || defined(__WIN32__) || defined(__NT__)
#define Q_DECL_EXPORT __declspec(dllexport)
#define Q_DECL_IMPORT __declspec(dllimport)
#else
#define Q_DECL_EXPORT __attribute__((visibility("default")))
#define Q_DECL_IMPORT __attribute__((visibility("default")))
#endif

#if defined(BUILD_SHARED_LIBS)
#if defined(QCLOUDMUSICAPI_LIBRARY)
#  define QCLOUDMUSICAPI_EXPORT Q_DECL_EXPORT
#else
#  define QCLOUDMUSICAPI_EXPORT Q_DECL_IMPORT
#endif
#else
#  define QCLOUDMUSICAPI_EXPORT
#endif

class QCLOUDMUSICAPI_EXPORT NeteaseCloudMusicApi: public QObject {
    Q_OBJECT
public:

    // 初始化名字
    Q_INVOKABLE QVariantMap activate_init_profile(QVariantMap);

    // 专辑动态信息
    Q_INVOKABLE QVariantMap album_detail_dynamic(QVariantMap);

    // 数字专辑详情
    Q_INVOKABLE QVariantMap album_detail(QVariantMap);

    // 数字专辑-语种风格馆
    Q_INVOKABLE QVariantMap album_list_style(QVariantMap);

    // 数字专辑-新碟上架
    Q_INVOKABLE QVariantMap album_list(QVariantMap);

    // 全部新碟
    Q_INVOKABLE QVariantMap album_new(QVariantMap);

    // 最新专辑
    Q_INVOKABLE QVariantMap album_newest(QVariantMap);

    // 数字专辑&数字单曲-榜单
    Q_INVOKABLE QVariantMap album_songsaleboard(QVariantMap);

    // 收藏/取消收藏专辑
    Q_INVOKABLE QVariantMap album_sub(QVariantMap);

    // 已收藏专辑列表
    Q_INVOKABLE QVariantMap album_sublist(QVariantMap);

    // 专辑内容
    Q_INVOKABLE QVariantMap album(QVariantMap);

    // 歌手专辑列表
    Q_INVOKABLE QVariantMap artist_album(QVariantMap);

    // 歌手介绍
    Q_INVOKABLE QVariantMap artist_desc(QVariantMap);

    // 歌手详情
    Q_INVOKABLE QVariantMap artist_detail(QVariantMap);

    // 歌手粉丝
    Q_INVOKABLE QVariantMap artist_fans(QVariantMap);

    // 歌手粉丝数量
    Q_INVOKABLE QVariantMap artist_follow_count(QVariantMap);

    // 歌手分类

    /*
    type 取值
    1:男歌手
    2:女歌手
    3:乐队

    area 取值
    -1:全部
    7华语
    96欧美
    8:日本
    16韩国
    0:其他

    initial 取值 a-z/A-Z
*/
    Q_INVOKABLE QVariantMap artist_list(QVariantMap);

    // 歌手相关MV
    Q_INVOKABLE QVariantMap artist_mv(QVariantMap);

    // 关注歌手新 MV
    Q_INVOKABLE QVariantMap artist_new_mv(QVariantMap);

    // 关注歌手新歌
    Q_INVOKABLE QVariantMap artist_new_song(QVariantMap);

    // 歌手全部歌曲
    Q_INVOKABLE QVariantMap artist_songs(QVariantMap);

    // 收藏与取消收藏歌手
    Q_INVOKABLE QVariantMap artist_sub(QVariantMap);

    // 关注歌手列表
    Q_INVOKABLE QVariantMap artist_sublist(QVariantMap);

    // 歌手热门 50 首歌曲
    Q_INVOKABLE QVariantMap artist_top_song(QVariantMap);

    // 歌手相关视频
    Q_INVOKABLE QVariantMap artist_video(QVariantMap);

    // 歌手单曲
    Q_INVOKABLE QVariantMap artists(QVariantMap);

    // 批量请求接口
    Q_INVOKABLE QVariantMap batch(QVariantMap);

    // 首页轮播图
    Q_INVOKABLE QVariantMap banner(QVariantMap);

    // 音乐日历
    Q_INVOKABLE QVariantMap calendar(QVariantMap);

    // 发送验证码
    Q_INVOKABLE QVariantMap captcha_sent(QVariantMap);

    // 校验验证码
    Q_INVOKABLE QVariantMap captcha_verify(QVariantMap);

    // 检测手机号码是否已注册
    Q_INVOKABLE QVariantMap cellphone_existence_check(QVariantMap);

    // 歌曲可用性
    Q_INVOKABLE QVariantMap check_music(QVariantMap);

    // 云盘歌曲信息匹配纠正
    Q_INVOKABLE QVariantMap cloud_match(QVariantMap);

    // 云盘上传
    // Q_INVOKABLE QVariantMap cloud(QVariantMap);

    // 搜索
    Q_INVOKABLE QVariantMap cloudsearch(QVariantMap);

    // 专辑评论
    Q_INVOKABLE QVariantMap comment_album(QVariantMap);

    // 电台评论
    Q_INVOKABLE QVariantMap comment_dj(QVariantMap);

    // 获取动态评论
    Q_INVOKABLE QVariantMap comment_event(QVariantMap);

    // 楼层评论
    Q_INVOKABLE QVariantMap comment_floor(QVariantMap);

    // 热门评论
    Q_INVOKABLE QVariantMap comment_hot(QVariantMap);

    // 评论抱一抱列表
    Q_INVOKABLE QVariantMap comment_hug_list(QVariantMap);

    // 点赞与取消点赞评论
    Q_INVOKABLE QVariantMap comment_like(QVariantMap);

    // 歌曲评论
    Q_INVOKABLE QVariantMap comment_music(QVariantMap);

    // MV评论
    Q_INVOKABLE QVariantMap comment_mv(QVariantMap);

    // 新版评论接口
    Q_INVOKABLE QVariantMap comment_new(QVariantMap);

    // 歌单评论
    Q_INVOKABLE QVariantMap comment_playlist(QVariantMap);

    // 视频评论
    Q_INVOKABLE QVariantMap comment_video(QVariantMap);

    // 发送与删除评论
    Q_INVOKABLE QVariantMap comment(QVariantMap);

    // 国家编码列表
    Q_INVOKABLE QVariantMap countries_code_list(QVariantMap);

    // 获取达人用户信息
    Q_INVOKABLE QVariantMap creator_authinfo_get(QVariantMap);

    // 签到

    /*
    0为安卓端签到 3点经验, 1为网页签到,2点经验
    签到成功 {'android': {'point': 3, 'code': 200}, 'web': {'point': 2, 'code': 200}}
    重复签到 {'android': {'code': -2, 'msg': '重复签到'}, 'web': {'code': -2, 'msg': '重复签到'}}
    未登录 {'android': {'code': 301}, 'web': {'code': 301}}
    */
    Q_INVOKABLE QVariantMap daily_signin(QVariantMap);

    // 数字专辑详情
    Q_INVOKABLE QVariantMap digitalAlbum_detail(QVariantMap);

    // 购买数字专辑
    Q_INVOKABLE QVariantMap digitalAlbum_ordering(QVariantMap);

    // 我的数字专辑
    Q_INVOKABLE QVariantMap digitalAlbum_purchased(QVariantMap);

    // 数字专辑销量
    Q_INVOKABLE QVariantMap digitalAlbum_sales(QVariantMap);

    // 电台banner
    Q_INVOKABLE QVariantMap dj_banner(QVariantMap);

    // 电台非热门类型
    Q_INVOKABLE QVariantMap dj_category_excludehot(QVariantMap);

    // 电台推荐类型
    Q_INVOKABLE QVariantMap dj_category_recommend(QVariantMap);

    // 电台分类列表
    Q_INVOKABLE QVariantMap dj_catelist(QVariantMap);

    // 电台详情
    Q_INVOKABLE QVariantMap dj_detail(QVariantMap);

    // 热门电台
    Q_INVOKABLE QVariantMap dj_hot(QVariantMap);

    // 付费电台
    Q_INVOKABLE QVariantMap dj_paygift(QVariantMap);

    // 电台个性推荐
    Q_INVOKABLE QVariantMap dj_personalize_recommend(QVariantMap);

    // 电台节目详情
    Q_INVOKABLE QVariantMap dj_program_detail(QVariantMap);

    // 电台24小时节目榜
    Q_INVOKABLE QVariantMap dj_program_toplist_hours(QVariantMap);

    // 电台节目榜
    Q_INVOKABLE QVariantMap dj_program_toplist(QVariantMap);

    // 电台节目列表
    Q_INVOKABLE QVariantMap dj_program(QVariantMap);

    // 类别热门电台
    Q_INVOKABLE QVariantMap dj_radio_hot(QVariantMap);

    // 精选电台分类

    /*
    有声书 10001
    知识技能 453050
    商业财经 453051
    人文历史 11
    外语世界 13
    亲子宝贝 14
    创作|翻唱 2001
    音乐故事 2
    3D|电子 10002
    相声曲艺 8
    情感调频 3
    美文读物 6
    脱口秀 5
    广播剧 7
    二次元 3001
    明星做主播 1
    娱乐|影视 4
    科技科学 453052
    校园|教育 4001
    旅途|城市 12
    */
    Q_INVOKABLE QVariantMap dj_recommend_type(QVariantMap);

    // 精选电台
    Q_INVOKABLE QVariantMap dj_recommend(QVariantMap);

    // 订阅与取消电台
    Q_INVOKABLE QVariantMap dj_sub(QVariantMap);

    // 订阅电台列表
    Q_INVOKABLE QVariantMap dj_sublist(QVariantMap);

    // 电台详情
    Q_INVOKABLE QVariantMap dj_subscriber(QVariantMap);

    // 电台今日优选
    Q_INVOKABLE QVariantMap dj_today_perfered(QVariantMap);

    // 电台24小时主播榜
    Q_INVOKABLE QVariantMap dj_toplist_hours(QVariantMap);

    // 电台新人榜
    Q_INVOKABLE QVariantMap dj_toplist_newcomer(QVariantMap);

    // 付费精品
    Q_INVOKABLE QVariantMap dj_toplist_pay(QVariantMap);

    // 电台最热主播榜
    Q_INVOKABLE QVariantMap dj_toplist_popular(QVariantMap);

    // 新晋电台榜/热门电台榜
    Q_INVOKABLE QVariantMap dj_toplist(QVariantMap);

    // 删除动态
    Q_INVOKABLE QVariantMap event_del(QVariantMap);

    // 转发动态
    Q_INVOKABLE QVariantMap event_forward(QVariantMap);

    // 动态
    Q_INVOKABLE QVariantMap event(QVariantMap);

    // 粉丝年龄比例
    Q_INVOKABLE QVariantMap fanscenter_basicinfo_age_get(QVariantMap);

    // 粉丝性别比例
    Q_INVOKABLE QVariantMap fanscenter_basicinfo_gender_get(QVariantMap);

    // 粉丝省份比例
    Q_INVOKABLE QVariantMap fanscenter_basicinfo_province_get(QVariantMap);

    // 粉丝数量
    Q_INVOKABLE QVariantMap fanscenter_overview_get(QVariantMap);

    // 粉丝来源
    Q_INVOKABLE QVariantMap fanscenter_trend_list(QVariantMap);

    // 垃圾桶
    Q_INVOKABLE QVariantMap fm_trash(QVariantMap);

    // 关注与取消关注用户
    Q_INVOKABLE QVariantMap follow(QVariantMap);

    // 根据nickname获取userid
    Q_INVOKABLE QVariantMap get_userids(QVariantMap);

    // 历史每日推荐歌曲详情
    Q_INVOKABLE QVariantMap history_recommend_songs_detail(QVariantMap);

    // 历史每日推荐歌曲
    Q_INVOKABLE QVariantMap history_recommend_songs(QVariantMap);

    // 首页-发现 block page
    // 这个接口为移动端接口，首页-发现页，数据结构可以参考 https://github.com/hcanyz/flutter-netease-music-api/blob/master/lib/src/api/uncategorized/bean.dart#L259 HomeBlockPageWrap
    // query.refresh 是否刷新数据
    Q_INVOKABLE QVariantMap homepage_block_page(QVariantMap);

    // 首页-发现 dragon ball
    // 这个接口为移动端接口，首页-发现页（每日推荐、歌单、排行榜 那些入口）
    // 数据结构可以参考 https://github.com/hcanyz/flutter-netease-music-api/blob/master/lib/src/api/uncategorized/bean.dart#L290 HomeDragonBallWrap
    // !需要登录或者游客登录，非登录返回 []
    Q_INVOKABLE QVariantMap homepage_dragon_ball(QVariantMap);

    //热门话题
    Q_INVOKABLE QVariantMap hot_topic(QVariantMap);

    //抱一抱评论
    Q_INVOKABLE QVariantMap hug_comment(QVariantMap);

    // 红心与取消红心歌曲
    Q_INVOKABLE QVariantMap like(QVariantMap);

    // 喜欢音乐列表
    Q_INVOKABLE QVariantMap likelist(QVariantMap);

    // 手机登录
    Q_INVOKABLE QVariantMap login_cellphone(QVariantMap);

    // 二维码检测扫码状态接口
    Q_INVOKABLE QVariantMap login_qr_check(QVariantMap);

    // 二维码生成接口
    Q_INVOKABLE QVariantMap login_qr_create(QVariantMap);

    // 二维码 key 生成接口
    Q_INVOKABLE QVariantMap login_qr_key(QVariantMap);

    // 登录刷新
    Q_INVOKABLE QVariantMap login_refresh(QVariantMap);

    // 登录状态
    Q_INVOKABLE QVariantMap login_status(QVariantMap);

    // 退出登录
    Q_INVOKABLE QVariantMap logout(QVariantMap);

    // 新版歌词 - 包含逐字歌词
    Q_INVOKABLE QVariantMap lyric_new(QVariantMap);

    // 歌词
    Q_INVOKABLE QVariantMap lyric(QVariantMap);

    // 重复昵称检测
    Q_INVOKABLE QVariantMap nickname_check(QVariantMap);

    Q_INVOKABLE QVariantMap recommend_resource(QVariantMap);

    // 游客登录
    Q_INVOKABLE QVariantMap register_anonimous(QVariantMap);

    // 相关歌单
    Q_INVOKABLE QVariantMap related_playlist(QVariantMap);

    // 搜索
    Q_INVOKABLE QVariantMap search(QVariantMap);

    // 获取客户端歌曲下载链接
    Q_INVOKABLE QVariantMap song_download_url(QVariantMap);

    // 歌曲链接 - v1
    // 此版本不再采用 br 作为音质区分的标准
    // 而是采用 standard, exhigh, lossless, hires, jyeffect(高清环绕声(QVariantMap);, sky(沉浸环绕声(QVariantMap);, jymaster(超清母带(QVariantMap); 进行音质判断
    Q_INVOKABLE QVariantMap song_url_v1(QVariantMap);

    // 音乐百科基础信息
    Q_INVOKABLE QVariantMap song_wiki_summary(QVariantMap);

    // 年度听歌报告2017-2022
    Q_INVOKABLE QVariantMap summary_annual(QVariantMap);

    // 获取达人达标信息
    Q_INVOKABLE QVariantMap threshold_detail_get(QVariantMap);

    // 所有榜单介绍
    Q_INVOKABLE QVariantMap toplist(QVariantMap);

    Q_INVOKABLE QVariantMap top_playlist(QVariantMap);

    // 专辑简要百科信息
    Q_INVOKABLE QVariantMap ugc_album_get(QVariantMap);

    // 歌手简要百科信息
    Q_INVOKABLE QVariantMap ugc_artist_get(QVariantMap);

    // 搜索歌手
    // 可传关键字或者歌手id
    Q_INVOKABLE QVariantMap ugc_artist_search(QVariantMap);

    // mv简要百科信息
    Q_INVOKABLE QVariantMap ugc_mv_get(QVariantMap);

    // 歌曲简要百科信息
    Q_INVOKABLE QVariantMap ugc_song_get(QVariantMap);

    // 获取账号信息
    Q_INVOKABLE QVariantMap user_account(QVariantMap);

    // 用户创建的电台
    Q_INVOKABLE QVariantMap user_audio(QVariantMap);

    // 获取用户绑定信息
    Q_INVOKABLE QVariantMap user_binding(QVariantMap);

    // 云盘歌曲删除
    Q_INVOKABLE QVariantMap user_cloud_del(QVariantMap);

    // 云盘数据详情
    Q_INVOKABLE QVariantMap user_cloud_detail(QVariantMap);

    // 云盘数据
    Q_INVOKABLE QVariantMap user_cloud(QVariantMap);

    // 获取用户历史评论
    Q_INVOKABLE QVariantMap user_comment_history(QVariantMap);

    // 用户详情
    Q_INVOKABLE QVariantMap user_detail(QVariantMap);

    // 用户电台节目
    Q_INVOKABLE QVariantMap user_dj(QVariantMap);

    // 用户动态
    Q_INVOKABLE QVariantMap user_event(QVariantMap);

    // 关注TA的人(粉丝)
    Q_INVOKABLE QVariantMap user_followeds(QVariantMap);

    // TA关注的人(关注)
    Q_INVOKABLE QVariantMap user_follows(QVariantMap);

    // 获取用户等级信息
    Q_INVOKABLE QVariantMap user_level(QVariantMap);

    // 用户歌单
    Q_INVOKABLE QVariantMap user_playlist(QVariantMap);

    // 听歌排行
    Q_INVOKABLE QVariantMap user_record(QVariantMap);

    // 用户绑定手机
    Q_INVOKABLE QVariantMap user_replacephone(QVariantMap);

    // 收藏计数
    Q_INVOKABLE QVariantMap user_subcount(QVariantMap);

    // 编辑用户信息
    Q_INVOKABLE QVariantMap user_update(QVariantMap);

    // 云贝 todo 任务
    Q_INVOKABLE QVariantMap yunbei_tasks_todo(QVariantMap);

    // 云贝所有任务
    Q_INVOKABLE QVariantMap yunbei_tasks(QVariantMap);

    // 云贝今日签到信息
    Q_INVOKABLE QVariantMap yunbei_today(QVariantMap);

    // 云贝
    Q_INVOKABLE QVariantMap yunbei(QVariantMap);
};
#endif // MODULE_H
