# rpdownload-STA1N
在STA1N的Roleplay Hub，中万相广场免登录，免下载次数，可以直接下载
用于这个网站https://sta1n156.github.io/RP-Hub/   应该通用
为什么我要创建此项目，STA1N群管理乱踢人，报告BUG也踢，这是贵站的bug:
UI 模板的 HTML 内容会被写入 iframe.srcdoc 执行。
该 iframe 同时启用了 allow-scripts 和 allow-same-origin，使上传者的 JavaScript 与主站同源，可读取页面 DOM、localStorage、sessionStorage 中的令牌，并能以当前用户身份调用接口。
受影响路径包括公开的 /api/ui-templates/:id 模板预览。任何被审核通过的恶意模板都可能攻击浏览者，管理员预览时尤其危险。
公开泄露上传者真实 IP
未登录请求 /api/ui-templates?sort=latest 返回 uploader_ip_address 字段。
我验证到响应中直接包含上传者 IP，例如 117.182.45.72。
这会暴露用户个人信息，并支持批量收集与关联用户活动。
