# [YRSSF](https://github.com/cgoxopx/YRSSF)
这是一个p2p架构的 云教学系统/直播平台框架  
支持POSIX，比如Android,Linux。  
目前尚不支持Windows，可通过VNC来使用Windows程序  
## 文档：
[API](build)
## 警告：
lock目录下的东西极度危险！极度危险！极度危险！重要的事说三遍
经测试，在原版linux下可导致[百度](https://www.baidu.com)等常见网站DNS解析错误，并且会自动关闭大多数端口
在android下会同时卸载包括`蓝牙`在内的大多数系统app，并且结束未允许的用户app的进程（具体效果见睿易派）
如果不需要限制设备功能（比如说防止学生用平板电脑打游戏……），请不要在lock目录里面乱make，否则后果很严重……
如果真的make了，在里面`make clean`一下可以解除锁机效果
---------------------
此工具请勿用于非法用途，否则后果自负
