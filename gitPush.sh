x#!/usr/bin/env bash

echo -n "请输入您的上传日志:"
read updateLog
git add *
git commit -m "$updateLog"
git push -u origin main
