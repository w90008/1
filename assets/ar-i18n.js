(function () {
  'use strict';

  const SKIP_TAGS = new Set(['SCRIPT', 'STYLE', 'CODE', 'PRE', 'TEXTAREA']);
  const ATTRS = ['title', 'placeholder', 'aria-label', 'alt', 'value'];

  const exactPairs = [
    [`Homebrew Launcher`, `مشغّل الهومبرو`],
    [`Payload launcher`, `مشغّل الحِزم التنفيذية`],
    [`Sonic Loader`, `سونيك لودر`],
    [`Sonic Loader — stats`, `سونيك لودر — الإحصائيات`],
    [`Sonic Loader — klog`, `سونيك لودر — سجل النواة`],
    [`Sonic Loader — PKG Zone`, `سونيك لودر — منطقة PKG`],
    [`Sonic Loader · File Manager`, `سونيك لودر · مدير الملفات`],
    [`← Home`, `← الرئيسية`],
    [`ELF Loader`, `مشغّل ELF`],
    [`Klog`, `سجل النواة`],
    [`Stats`, `الإحصائيات`],
    [`🛒 PKG Zone`, `🛒 منطقة PKG`],
    [`📁 Files`, `📁 الملفات`],
    [`⚙ Settings`, `⚙ الإعدادات`],
    [`SaveMgr`, `مدير الحفظ`],
    [`⚠ Disclaimer`, `⚠ إخلاء مسؤولية`],
    [`📊 Activity`, `📊 النشاط`],
    [`Notifications`, `الإشعارات`],
    [`Mark all read`, `تحديد الكل كمقروء`],
    [`Clear`, `مسح`],
    [`No notifications yet.`, `لا توجد إشعارات بعد.`],
    [`🏠 Home`, `🏠 الرئيسية`],
    [`Homebrew`, `هومبرو`],
    [`File Manager`, `مدير الملفات`],
    [`Open Sonic Loader settings`, `فتح إعدادات سونيك لودر`],
    [`Browse and install PS5 homebrew from pkg-zone.com`, `تصفّح وثبّت تطبيقات هومبرو لـ PS5 من pkg-zone.com`],
    [`Installed titles`, `الألعاب المثبتة`],
    [`Read directly from the PS5 app database. Tap a tile to launch.`, `تُقرأ مباشرة من قاعدة بيانات تطبيقات PS5. اضغط على أي بطاقة للتشغيل.`],
    [`Now playing`, `قيد التشغيل الآن`],
    [`▶ Launch`, `▶ تشغيل`],
    [`Settings`, `الإعدادات`],
    [`Appearance`, `المظهر`],
    [`JB & kernel`, `الجيلبريك والنواة`],
    [`Games & storage`, `الألعاب والتخزين`],
    [`Mods`, `التعديلات`],
    [`Profile`, `الملف الشخصي`],
    [`System`, `النظام`],
    [`Dark`, `داكن`],
    [`OLED Black`, `أسود OLED`],
    [`Light`, `فاتح`],
    [`High Contrast`, `تباين عالٍ`],
    [`Theme is saved per-browser via localStorage.`, `يتم حفظ السمة لكل متصفح عبر التخزين المحلي.`],
    [`How Long To Beat (HLTB)`, `How Long To Beat (HLTB)`],
    [`Vercel proxy + per-title progress bars in Activity`, `بروكسي Vercel + أشرطة تقدّم لكل لعبة في صفحة النشاط`],
    [`HLTB Overlay in Activity`, `إظهار HLTB داخل النشاط`],
    [`Off`, `إيقاف`],
    [`On`, `تشغيل`],
    [`Save`, `حفظ`],
    [`Kstuff & auto-toggle`, `Kstuff والتبديل التلقائي`],
    [`kernel patcher + klog auto-pause`, `مُصحّح النواة + إيقاف klog تلقائيًا`],
    [`Kstuff auto-toggle`, `التبديل التلقائي لـ Kstuff`],
    [`Start pause delay (seconds)`, `مهلة بدء الإيقاف المؤقت (بالثواني)`],
    [`End pause delay (seconds)`, `مهلة إنهاء الإيقاف المؤقت (بالثواني)`],
    [`💾 Save delays`, `💾 حفظ المهلات`],
    [`Install kstuff-lite + ShadowMountPlus`, `تثبيت kstuff-lite + ShadowMountPlus`],
    [`Required`, `مطلوب`],
    [`Loading releases…`, `جارٍ تحميل الإصدارات…`],
    [`⬇ Install`, `⬇ تثبيت`],
    [`↻ Refresh`, `↻ تحديث`],
    [`📀 ShadowMountPlus`, `📀 ShadowMountPlus`],
    [`auto-restart SMP after install`, `إعادة تشغيل SMP تلقائيًا بعد التثبيت`],
    [`↺ Reset both`, `↺ إعادة ضبط الاثنين`],
    [`ShadowMountPlus`, `ShadowMountPlus`],
    [`SMP daemon`, `خدمة SMP`],
    [`Stopped`, `متوقف`],
    [`Scan paths`, `مسارات الفحص`],
    [`Defaults toggle`, `تبديل المسارات الافتراضية`],
    [`Use default scan paths`, `استخدام مسارات الفحص الافتراضية`],
    [`Show default paths`, `إظهار المسارات الافتراضية`],
    [`Saved manual paths`, `المسارات اليدوية المحفوظة`],
    [`Manual scan paths`, `مسارات فحص يدوية`],
    [`No manual scan paths.`, `لا توجد مسارات فحص يدوية.`],
    [`"+ Add" multi-row editor`, `محرر متعدد الأسطر لزر "+ إضافة"`],
    [`＋ Add another path`, `＋ إضافة مسار آخر`],
    [`💾 Save all pending`, `💾 حفظ كل التغييرات المعلّقة`],
    [`auto-restart SMP`, `إعادة تشغيل SMP تلقائيًا`],
    [`↺ Restart SMP`, `↺ إعادة تشغيل SMP`],
    [`⌫ Clear manual`, `⌫ مسح اليدوي`],
    [`Metadata self-healer`, `إصلاح البيانات الوصفية تلقائيًا`],
    [`idle`, `خامل`],
    [`Status`, `الحالة`],
    [`Poll interval (seconds)`, `فاصل الاستطلاع (بالثواني)`],
    [`Default`, `الافتراضي`],
    [`Cheat engine`, `محرك الغش`],
    [`Loading status…`, `جارٍ تحميل الحالة…`],
    [`How to upload cheat files (via FTP)`, `طريقة رفع ملفات الغش عبر FTP`],
    [`Auto-download entire cheat repository`, `تنزيل مستودع الغش بالكامل تلقائيًا`],
    [`Download whole repo`, `تنزيل المستودع بالكامل`],
    [`Search staged cheats`, `البحث في ملفات الغش المجهزة`],
    [`Cheats for the running game`, `ملفات الغش للعبة الحالية`],
    [`Y2JB autoloader sync`, `مزامنة محمّل Y2JB التلقائي`],
    [`Detected directories`, `المجلدات المكتشفة`],
    [`↺ Rescan`, `↺ إعادة الفحص`],
    [`⬇ Update all`, `⬇ تحديث الكل`],
    [`Managed Payloads`, `الحِزم التنفيذية المُدارة`],
    [`Auto-update on boot`, `تحديث تلقائي عند الإقلاع`],
    [`➕ Browse to add a payload`, `➕ استعراض لإضافة حزمة تنفيذية`],
    [`🔍 Lookup latest`, `🔍 جلب أحدث إصدار`],
    [`Add`, `إضافة`],
    [`➕ Add to registry`, `➕ إضافة إلى السجل`],
    [`📁 …or upload a payload from this PC`, `📁 …أو ارفع حزمة تنفيذية من هذا الجهاز`],
    [`⬆ Upload & register`, `⬆ رفع وتسجيل`],
    [`PKG Zone catalog`, `فهرس منطقة PKG`],
    [`cached weekly · manual refresh`, `تخزين أسبوعي مؤقت · تحديث يدوي`],
    [`Cache status`, `حالة الذاكرة المؤقتة`],
    [`⟳ Refresh now`, `⟳ تحديث الآن`],
    [`BackPork`, `BackPork`],
    [`Disabled by default`, `معطّل افتراضيًا`],
    [`nanoDNS`, `nanoDNS`],
    [`nanoDNS daemon`, `خدمة nanoDNS`],
    [`Running`, `قيد التشغيل`],
    [`Sonic Loader UI pkg auto-update`, `التحديث التلقائي لحزمة واجهة سونيك لودر`],
    [`Auto-update Sonic Loader UI pkg`, `تحديث حزمة واجهة سونيك لودر تلقائيًا`],
    [`App Dumper`, `تفريغ التطبيقات`],
    [`Run app dumper`, `تشغيل أداة التفريغ`],
    [`▶ Spawn ps5-app-dumper.elf`, `▶ تشغيل ps5-app-dumper.elf`],
    [`Re-seed configs`, `إعادة إنشاء ملفات الإعداد`],
    [`Loading dumper configs…`, `جارٍ تحميل إعدادات أداة التفريغ…`],
    [`Homebrew installer`, `مثبّت الهومبرو`],
    [`download & extract payloads + launcher PKG`, `تنزيل واستخراج الحِزم التنفيذية + حزمة المشغّل`],
    [`Select User`, `اختر المستخدم`],
    [`Loading users…`, `جارٍ تحميل المستخدمين…`],
    [`Recently Played`, `تم لعبها مؤخرًا`],
    [`↺ Refresh HLTB`, `↺ تحديث HLTB`],
    [`Loading…`, `جارٍ التحميل…`],
    [`Game Activity`, `نشاط اللعبة`],
    [`Select a game to view activity`, `اختر لعبة لعرض نشاطها`],
    [`Year in Review`, `مراجعة السنة`],
    [`Select Year:`, `اختر السنة:`],
    [`📅 Monthly Gaming Activity`, `📅 نشاط اللعب الشهري`],
    [`🍰 Time Distribution`, `🍰 توزيع الوقت`],
    [`Progress Tracker`, `متتبع التقدّم`],
    [`Select Game:`, `اختر اللعبة:`],
    [`Disclaimer`, `إخلاء مسؤولية`],
    [`Intended uses`, `الاستخدامات المقصودة`],
    [`Not intended for, and the author does not condone`, `غير مخصص لما يلي، ولا يوافق المؤلف عليه`],
    [`You are solely responsible`, `أنت المسؤول وحدك`],
    [`Got it`, `فهمت`],
    [`Please wait — opening folder…`, `يرجى الانتظار — جارٍ فتح المجلد…`],
    [`Left`, `اليسار`],
    [`Right`, `اليمين`],
    [`Back to home`, `العودة إلى الرئيسية`],
    [`Pause`, `إيقاف مؤقت`],
    [`Download`, `تنزيل`],
    [`📜 Klog`, `📜 سجل النواة`],
    [`📊 Stats`, `📊 الإحصائيات`],
    [`CPU Temp`, `حرارة المعالج`],
    [`SoC Temp`, `حرارة SoC`],
    [`Hottest`, `الأعلى حرارة`],
    [`highest sensor reading`, `أعلى قراءة من الحساسات`],
    [`Uptime`, `مدة التشغيل`],
    [`since boot`, `منذ الإقلاع`],
    [`Loader PID`, `معرّف عملية المحمّل`],
    [`Klog Buffer`, `مخزن سجل النواة`],
    [`paused`, `متوقف مؤقتًا`],
    [`live`, `مباشر`],
    [`← Back`, `← رجوع`],
    [`🛒 PKG Zone`, `🛒 منطقة PKG`],
    [`Homebrew catalog mirrored from pkg-zone.com — PS5 + PS4 packages`, `فهرس هومبرو منسوخ من pkg-zone.com — حزم PS5 وPS4`],
    [`Search title / author / id…`, `ابحث بالعنوان / المؤلف / المعرّف…`],
    [`All`, `الكل`],
    [`⟳ Refresh`, `⟳ تحديث`],
    [`Loading catalog…`, `جارٍ تحميل الفهرس…`],
    [`Install`, `تثبيت`],
    [`PS5 Temperature`, `حرارة PS5`],
    [`hottest of CPU + SoC sensors`, `أعلى حرارة بين حساسات CPU وSoC`],
    [`min · 60s`, `الأدنى · 60ث`],
    [`avg`, `المتوسط`],
    [`max · 60s`, `الأقصى · 60ث`],
    [`SMB Share`, `مشاركة SMB`],
    [`Sonic Loader build version`, `إصدار بناء سونيك لودر`],
    [`PKG installer rejected the file`, `رفض مثبّت PKG الملف`],
    [`Open Garlic SaveMgr (port 8082)`, `فتح Garlic SaveMgr (المنفذ 8082)`],
    [`No file selected.`, `لم يتم اختيار ملف.`],
    [`Updated url has been saved.`, `تم حفظ الرابط المحدّث.`],
    [`Terminal input`, `إدخال الطرفية`],
    [`Too much output to announce, navigate to rows manually to read`, `الناتج كبير جدًا للإعلان عنه، انتقل يدويًا بين الأسطر للقراءة`],
    [`قناة: محمد خالد للشروحات`, `قناة: محمد خالد للشروحات`]
  ];

  const exactMap = new Map(exactPairs);

  const regexRules = [
    [/^updated\s+(.+)$/i, (_, t) => `آخر تحديث ${t}`],
    [/^(\d+) uploaded$/i, (_, n) => `تم رفع ${n}`],
    [/^(\d+) uploaded,\s*(\d+) failed$/i, (_, a, b) => `تم رفع ${a}، وفشل ${b}`],
    [/^Cancelled —\s*(\d+) OK,\s*(\d+) failed$/i, (_, a, b) => `أُلغي — نجح ${a} وفشل ${b}`],
    [/^No download URL for\s+(.+)$/i, (_, t) => `لا يوجد رابط تنزيل لـ ${t}`],
    [/^Failed to load:\s+(.+)$/i, (_, e) => `فشل التحميل: ${e}`],
    [/^No matches for "(.+)" in this filter\.$/i, (_, q) => `لا توجد نتائج لـ "${q}" ضمن هذا الفلتر.`],
    [/^Delete\s+(\d+)\s+item\(s\)\s+from\s+(.+)\?$/i, (_, n, p) => `حذف ${n} عنصر/عناصر من ${p}؟`],
    [/^New folder in\s+(.+):$/i, (_, p) => `مجلد جديد داخل ${p}:`],
    [/^Rename\s+"(.+)"\s+to:$/i, (_, n) => `إعادة تسمية "${n}" إلى:`],
    [/^Spawn\s+(.+)\?$/i, (_, p) => `تشغيل ${p}؟`],
    [/^Clear activation for slot\s+(\d+)\?$/i, (_, s) => `مسح التفعيل للمنفذ ${s}؟`],
    [/^Remove\s+"(.+)"\s+from the managed-payloads registry\?/i, (_, n) => `إزالة "${n}" من سجل الحِزم التنفيذية المُدارة؟`],
    [/^Select something on the source side first$/i, () => `اختر شيئًا أولًا من جهة المصدر`],
    [/^Select something to delete$/i, () => `اختر شيئًا للحذف`],
    [/^Select exactly one item to rename$/i, () => `اختر عنصرًا واحدًا فقط لإعادة تسميته`],
    [/^No active pane to upload into$/i, () => `لا توجد لوحة نشطة للرفع إليها`],
    [/^error$/i, () => `خطأ`],
    [/^hot$/i, () => `مرتفع`],
    [/^warm$/i, () => `دافئ`],
    [/^cool$/i, () => `بارد`],
    [/^http\s+(\d+)$/i, (_, n) => `HTTP ${n}`],
    [/^network$/i, () => `الشبكة`]
  ];

  const phrasePairs = [
    [`Open Sonic Loader settings`, `فتح إعدادات سونيك لودر`],
    [`Browse and install PS5 homebrew`, `تصفّح وثبّت هومبرو PS5`],
    [`PS5-native packages only`, `حزم PS5 فقط`],
    [`PS4 packages (install as fpkg on JB PS5)`, `حزم PS4 (تُثبَّت كـ fpkg على PS5 الجيلبريك)`],
    [`Everything in the catalog`, `كل ما في الفهرس`],
    [`Re-fetch the catalog from pkg-zone.com`, `إعادة جلب الفهرس من pkg-zone.com`],
    [`Loading catalog`, `جارٍ تحميل الفهرس`],
    [`Catalog is empty — try Refresh.`, `الفهرس فارغ — جرّب التحديث.`],
    [`Loading users`, `جارٍ تحميل المستخدمين`],
    [`Select User`, `اختر المستخدم`],
    [`Recently Played`, `تم لعبها مؤخرًا`],
    [`Select a game to view activity`, `اختر لعبة لعرض نشاطها`],
    [`Select Game`, `اختر اللعبة`],
    [`Select Year`, `اختر السنة`],
    [`Pause`, `إيقاف مؤقت`],
    [`Clear`, `مسح`],
    [`Download`, `تنزيل`],
    [`Loading`, `جارٍ التحميل`],
    [`Refresh`, `تحديث`],
    [`Install`, `تثبيت`],
    [`Back`, `رجوع`],
    [`Home`, `الرئيسية`],
    [`Settings`, `الإعدادات`],
    [`File Manager`, `مدير الملفات`],
    [`Homebrew`, `هومبرو`],
    [`Stats`, `الإحصائيات`],
    [`Klog`, `سجل النواة`],
    [`No matches`, `لا توجد نتائج`],
    [`failed`, `فشل`],
    [`uploaded`, `تم الرفع`],
    [`Please wait`, `يرجى الانتظار`],
    [`opening folder`, `جارٍ فتح المجلد`],
    [`Delete`, `حذف`],
    [`Rename`, `إعادة تسمية`],
    [`New folder`, `مجلد جديد`],
    [`Successfully authenticated. Do you want to save these credentials?`, `تمت المصادقة بنجاح. هل تريد حفظ بيانات الاعتماد هذه؟`],
    [`This share requires authentication. Do you want to enter credentials?`, `هذه المشاركة تتطلب مصادقة. هل تريد إدخال بيانات الاعتماد؟`],
    [`Enter username`, `أدخل اسم المستخدم`],
    [`Enter password`, `أدخل كلمة المرور`],
    [`Failed to authenticate to share:`, `فشل في المصادقة على المشاركة:`],
    [`Failed to load directory:`, `فشل في تحميل المجلد:`],
    [`Edit url:`, `تعديل الرابط:`],
    [`Are you sure you want to remove this share?`, `هل أنت متأكد من إزالة هذه المشاركة؟`],
    [`Enter the path to the share:`, `أدخل مسار المشاركة:`],
    [`Are you sure you want to add this share?`, `هل أنت متأكد من إضافة هذه المشاركة؟`],
    [`Invalid file type. Only .bin and .elf files are allowed.`, `نوع الملف غير صالح. المسموح فقط ملفات ‎.bin و‎.elf.`],
    [`Reset homebrew list?`, `إعادة ضبط قائمة الهومبرو؟`],
    [`Enter args`, `أدخل المعاملات`],
    [`Switch FTP to anonymous (no auth)?`, `تحويل FTP إلى وضع المجهول (بدون تسجيل دخول)؟`],
    [`Replace sonic-loader ELFs in every detected ps5_autoloader/ folder with the latest release?`, `استبدال ملفات ELF الخاصة بـ sonic-loader في كل مجلد ps5_autoloader/ مكتشف بآخر إصدار؟`],
    [`Reset both kstuff and SMP?`, `إعادة ضبط kstuff وSMP معًا؟`],
    [`Install drakmor kstuff v1.0.3 (super low FW)?`, `تثبيت drakmor kstuff v1.0.3 (للفيرموير المنخفض جدًا)؟`],
    [`Spawn ps5-app-dumper.elf?`, `تشغيل ps5-app-dumper.elf؟`],
    [`Spawn np-fake-signin.elf?`, `تشغيل np-fake-signin.elf؟`],
    [`Spawn np-restore-account.elf?`, `تشغيل np-restore-account.elf؟`],
    [`PS5 Temperature`, `حرارة PS5`],
    [`hottest of CPU + SoC sensors`, `أعلى حرارة بين حساسات CPU وSoC`],
    [`Loading releases`, `جارٍ تحميل الإصدارات`],
    [`Loading status`, `جارٍ تحميل الحالة`],
    [`No notifications yet`, `لا توجد إشعارات بعد`],
    [`Open Garlic SaveMgr`, `فتح Garlic SaveMgr`],
    [`Sonic Loader build version`, `إصدار بناء سونيك لودر`],
    [`Sonic Loader`, `سونيك لودر`]
  ];

  function normalize(s) {
    return String(s).replace(/\s+/g, ' ').trim();
  }

  function hasLatin(s) {
    return /[A-Za-z]/.test(s);
  }

  function translateCore(text) {
    if (!text) return text;
    const normalized = normalize(text);
    if (!normalized) return text;

    if (exactMap.has(normalized)) return exactMap.get(normalized);

    for (const [re, repl] of regexRules) {
      if (re.test(normalized)) return normalized.replace(re, repl);
    }

    let out = normalized;
    for (const [en, ar] of phrasePairs) {
      if (out.includes(en)) out = out.split(en).join(ar);
    }
    return out;
  }

  function preserveEdges(original, translated) {
    const lead = original.match(/^\s*/)?.[0] || '';
    const trail = original.match(/\s*$/)?.[0] || '';
    return lead + translated + trail;
  }

  function translateText(text) {
    if (!text || !hasLatin(text)) return text;
    const translated = translateCore(text);
    return translated === text ? text : preserveEdges(text, translated);
  }

  function shouldSkipNode(node) {
    const parent = node.parentElement;
    if (!parent) return true;
    if (SKIP_TAGS.has(parent.tagName)) return true;
    if (parent.closest('code, pre, .viewer, .xterm, .xterm-accessibility, .path-input, .mono, .url')) return true;
    return false;
  }

  function translateNode(node) {
    if (!node || node.nodeType !== Node.TEXT_NODE) return;
    if (shouldSkipNode(node)) return;
    const next = translateText(node.nodeValue || '');
    if (next !== node.nodeValue) node.nodeValue = next;
  }

  function translateAttrs(el) {
    if (!el || el.nodeType !== Node.ELEMENT_NODE) return;
    if (SKIP_TAGS.has(el.tagName)) return;
    for (const attr of ATTRS) {
      if (!el.hasAttribute(attr)) continue;
      const val = el.getAttribute(attr);
      if (!val || !hasLatin(val)) continue;
      if (attr === 'value' && !/^(button|submit|reset)$/i.test(el.type || '')) continue;
      const next = translateText(val);
      if (next !== val) el.setAttribute(attr, next);
    }
  }

  function walk(root) {
    if (!root) return;
    if (root.nodeType === Node.TEXT_NODE) {
      translateNode(root);
      return;
    }
    if (root.nodeType === Node.ELEMENT_NODE) {
      translateAttrs(root);
    }
    const walker = document.createTreeWalker(root, NodeFilter.SHOW_TEXT, null);
    let cur;
    while ((cur = walker.nextNode())) translateNode(cur);
    if (root.querySelectorAll) root.querySelectorAll('*').forEach(translateAttrs);
  }

  function patchDialogs() {
    if (window.__slArabicDialogsPatched) return;
    window.__slArabicDialogsPatched = true;
    const nativeAlert = window.alert?.bind(window);
    const nativeConfirm = window.confirm?.bind(window);
    const nativePrompt = window.prompt?.bind(window);
    if (nativeAlert) {
      window.alert = function (msg) {
        return nativeAlert(translateText(String(msg ?? '')));
      };
    }
    if (nativeConfirm) {
      window.confirm = function (msg) {
        return nativeConfirm(translateText(String(msg ?? '')));
      };
    }
    if (nativePrompt) {
      window.prompt = function (msg, defVal) {
        return nativePrompt(translateText(String(msg ?? '')), defVal);
      };
    }
  }

  function injectStyle() {
    if (document.getElementById('sl-ar-style')) return;
    const style = document.createElement('style');
    style.id = 'sl-ar-style';
    style.textContent = `
      html[lang="ar"] body { direction: rtl; text-align: right; }
      html[lang="ar"] input, html[lang="ar"] textarea,
      html[lang="ar"] code, html[lang="ar"] pre,
      html[lang="ar"] .viewer, html[lang="ar"] .path-input,
      html[lang="ar"] .xterm, html[lang="ar"] .mono, html[lang="ar"] .url {
        direction: ltr !important;
        text-align: left !important;
        unicode-bidi: plaintext;
      }
      #sl-ar-credit {
        position: fixed;
        left: 8px;
        bottom: 8px;
        z-index: 2147483645;
        padding: 4px 8px;
        border-radius: 6px;
        background: rgba(0,0,0,0.55);
        color: rgba(255,255,255,0.88);
        border: 1px solid rgba(255,255,255,0.10);
        font: 11px/1.2 Arial, sans-serif;
        pointer-events: none;
        user-select: none;
      }
    `;
    document.head.appendChild(style);
  }

  function injectCredit() {
    if (document.getElementById('sl-ar-credit')) return;
    const el = document.createElement('div');
    el.id = 'sl-ar-credit';
    el.textContent = 'قناة: محمد خالد للشروحات ';
    document.body.appendChild(el);
  }

  function applyRootLocale() {
    document.documentElement.lang = 'ar';
    document.documentElement.dir = 'rtl';
    document.title = translateText(document.title);
  }

  function observe() {
    const obs = new MutationObserver((mutations) => {
      for (const m of mutations) {
        if (m.type === 'characterData') {
          translateNode(m.target);
        } else if (m.type === 'attributes') {
          translateAttrs(m.target);
        } else if (m.type === 'childList') {
          m.addedNodes.forEach(walk);
        }
      }
    });
    obs.observe(document.documentElement, {
      childList: true,
      subtree: true,
      characterData: true,
      attributes: true,
      attributeFilter: ATTRS
    });
  }

  function boot() {
    applyRootLocale();
    injectStyle();
    patchDialogs();
    walk(document.documentElement);
    injectCredit();
    observe();
    setTimeout(() => {
      walk(document.documentElement);
      injectCredit();
      document.title = translateText(document.title);
    }, 600);
  }

  if (document.readyState === 'loading') {
    document.addEventListener('DOMContentLoaded', boot, { once: true });
  } else {
    boot();
  }
})();
