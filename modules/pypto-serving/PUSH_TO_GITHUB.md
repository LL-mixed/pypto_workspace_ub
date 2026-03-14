# 推送到 GitHub 的步骤

本地仓库已初始化并完成首次提交。请按下面步骤在 **你的 GitHub 账号** 下创建仓库并推送。

## 1. 在 GitHub 上创建空仓库

- 打开：**https://github.com/new**
- **Repository name** 填：`pypto-serving`
- 不要勾选 “Add a README” / “Add .gitignore”（本地已有）
- 点击 **Create repository**

## 2. 把本地仓库推上去

如果你的 GitHub 用户名**不是** `liaoheng`，先改远程地址（把 `YOUR_USERNAME` 换成你的用户名）：

```bash
cd /data/liaoheng/pypto_workspace/pypto-serving
git remote set-url origin git@github.com:YOUR_USERNAME/pypto-serving.git
```

然后推送：

```bash
git push -u origin main
```

如果使用 HTTPS 而不是 SSH，可改为：

```bash
git remote set-url origin https://github.com/YOUR_USERNAME/pypto-serving.git
git push -u origin main
```

推送时如要求登录，请按提示在浏览器或命令行完成认证。

## 3. 使用 GitHub CLI 创建并推送（可选）

若已安装并登录 [GitHub CLI](https://cli.github.com/)（`gh auth login`）：

```bash
cd /data/liaoheng/pypto_workspace/pypto-serving
git remote remove origin   # 若之前加过
gh repo create pypto-serving --public --source=. --remote=origin --push
```

---

完成后，在浏览器打开 `https://github.com/YOUR_USERNAME/pypto-serving` 即可看到代码。
