# DesktopToggler

ابزاری برای ویندوز: با دابل‌کلیک روی فضای خالی دسکتاپ، آیکون‌های دسکتاپ مخفی/نمایان و تسکبار auto-hide/ثابت می‌شه.

## دانلود exe آماده (بدون نیاز به نصب چیزی)

این ریپو با GitHub Actions تنظیم شده تا با هر push روی شاخه `main`، به‌صورت خودکار فایل exe ساخته بشه.

### روش ۱: از بخش Releases
بعد از اولین push، برو به تب **Releases** ریپو (سمت راست صفحه گیت‌هاب) و آخرین نسخه `DesktopToggler.exe` رو دانلود کن.

### روش ۲: از بخش Actions (Artifacts)
1. برو به تب **Actions**
2. روی آخرین اجرای موفق (✔️ سبز) کلیک کن
3. پایین صفحه، بخش **Artifacts** رو پیدا کن و `DesktopToggler-exe` رو دانلود کن (یک فایل zip حاوی exe بهت می‌ده)

## آپلود این پروژه در گیت‌هاب (یک بار، از صفر)

اگه قبلاً ریپو نساختی:

```bash
git init
git add .
git commit -m "Initial commit"
git branch -M main
git remote add origin https://github.com/USERNAME/DesktopToggler.git
git push -u origin main
```

به محض push شدن، تب **Actions** رو چک کن — بیلد به‌صورت خودکار شروع می‌شه (حدود ۱-۲ دقیقه طول می‌کشه).

## اجرای دستی بیلد (بدون push جدید)
توی تب **Actions** > روی ورک‌فلو **Build DesktopToggler.exe** > دکمه **Run workflow** رو بزن.

## ساخت محلی (روی ویندوز خودت، اختیاری)
اگه .NET SDK نصب داری:
```
dotnet publish -c Release
```
خروجی اینجا قرار می‌گیره:
```
bin\Release\net6.0-windows\win-x64\publish\DesktopToggler.exe
```
