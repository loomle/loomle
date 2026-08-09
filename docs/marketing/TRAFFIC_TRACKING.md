# Loomle 流量追踪规范

这份规范用于统一官网、Fab、GitHub、Epic Developer Community 和其他渠道的
流量标记。目标是让 GA4 中的来源可以长期比较，而不是每次发布临时起名。

## 数据分工

- **GA4**：网站访问来源、UTM campaign、页面路径和关键点击事件。
- **Google Search Console**：Google query、impression、click、CTR、position 和收录。
- **Bing Webmaster Tools（下一步）**：接入后用于查看 Bing Web / Chat 搜索表现、收录和技术 SEO。
- **Cloudflare Web Analytics**：通过 Cloudflare Automatic setup 提供不依赖 Cookie 的总访问基线、referrer、地区、设备和性能。
- **Fab / GitHub Analytics**：各平台内部的浏览、下载、clone 与 release 数据。

各平台统计口径不同，不应直接相加。跨平台判断趋势时，以日期、版本发布时间和
带 UTM 的官网访问为关联线索。

## UTM 命名

所有值使用小写英文和下划线，不使用空格。已有值不要因为文案变化而改名。

| 参数 | 含义 | 约定示例 |
| --- | --- | --- |
| `utm_source` | 具体平台 | `fab`、`github`、`epic_community`、`reddit`、`x`、`linkedin`、`discord`、`youtube` |
| `utm_medium` | 渠道类型 | `marketplace`、`repository`、`community`、`social`、`video`、`referral` |
| `utm_campaign` | 一次可比较的推广活动 | `fab_0_7_launch`、`sal_blog_launch`、`ue_5_8_support` |
| `utm_content` | 同一活动中的具体入口 | `listing_description`、`launch_post`、`profile_link`、`article_footer` |

示例：

```text
https://loomle.ai/blog/ai-agents-edit-blueprints-like-code/?utm_source=epic_community&utm_medium=community&utm_campaign=sal_blog_launch&utm_content=launch_post
```

```text
https://loomle.ai/install.html?utm_source=fab&utm_medium=marketplace&utm_campaign=fab_0_7_launch&utm_content=listing_description
```

## GA4 事件

官网自动记录以下事件；事件参数只包含去除 query 和 fragment 后的目标 URL、域名和
链接文字，不记录姓名、邮箱、Unreal 工程内容或 MCP 数据。

- `download_click`：GitHub Release 文件及 ZIP / 安装文件下载。
- `fab_click`：前往 Fab listing。
- `github_click`：前往 GitHub，但不含已归类为下载的链接。
- `install_click`：进入安装页面。
- `docs_start`：进入 Quickstart。
- `blog_click`：进入 Blog 文章或列表。

## 发布前检查

1. 外部宣传链接带完整的 `source + medium + campaign`。
2. 同一篇内容在不同渠道保留同一个 `campaign`，只改变 `source / medium / content`。
3. 不给站内链接添加 UTM，避免覆盖原始来源。
4. 不把版本号单独当作 `source`，版本属于 `campaign`。
5. 发布后用 GA4 Realtime 验证一次访问和一次关键点击。

## 每周观察

每周固定记录：访问用户、sessions、主要 source / medium、landing page、关键点击、
Google 搜索 query（接入 Bing 后同时记录 Bing query），以及 Fab/GitHub 当周下载。先看四周趋势，再判断一次更新或
宣传是否真正带来了持续曝光。
