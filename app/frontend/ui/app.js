'use strict';
const $=id=>document.getElementById(id);
const bridge=window.chrome?.webview;
const state={selectedTimeBank:'free',artEnabled:false,artFactor:null,mode:'services',page:'services',theme:'dark',language:'vi',logged:false,authStatus:'checking',initialized:false,loginPending:false,connectStage:'',connectSource:'',connectSawWorker:false,connectAttempt:0,logoutPending:false,game:'valorant',region:'SG',catalog:[],regions:[],wallet:null,connection:null,account:'',waiting:false,optimizeMemory:true,optimizeOnMinimize:false,walletStatus:'idle',history:[],graphEpoch:null,activeGame:'',activeRegion:'',activeNodeVisible:true,overlap:null};
const translations={
vi:{
'account.logout':'Đăng xuất','signin.title':'Đăng nhập vào GLO','signin.desc':'Nhận mã trên website rồi nhập vào ứng dụng. Mã chỉ dùng một lần trong 3 phút.','signin.button':'Đăng nhập','signin.website':'Nhận mã trên website ↗',
'wallet.label':'FREE TIME BANK','wallet.earn':'Nhận thời gian','wallet.minutes':'phút','wallet.available':'Còn {n} phút khả dụng','wallet.reserved':'Khả dụng cho phiên mới: {n} phút · Đang dùng: {r} phút','wallet.sync':'Đang đồng bộ với GLO...','wallet.updating':'Đang cập nhật...','wallet.updateError':'Không thể cập nhật, đang hiển thị số dư trước đó.',
'art.help':'Định tuyến nâng cao sử dụng nhiều relay node. Hệ số thời gian do GLO cung cấp; mỗi phút kết nối sẽ tiêu thụ số phút Time Bank tương ứng.','art.label':'Định tuyến nâng cao','stats.route':'TUYẾN','services.title':'GLO Services','services.subtitle':'Chọn trò chơi và khu vực máy chủ để bắt đầu.','services.region':'Khu vực máy chủ','services.connect':'Kết nối','services.disconnect':'Ngắt kết nối','overlap.title':'Đã có kết nối đang hoạt động','overlap.body':'Bạn đã tối ưu game {game}, ở phần {where}. Vui lòng dừng kết nối hiện tại trước khi thiết lập kết nối mới.',
'connect.title':'Custom Connect','connect.subtitle':'Dán URI do provider cấp hoặc mở liên kết glo://connect từ Windows.','connect.uri':'PROVIDER URI','connect.paste':'Dán từ clipboard','connect.inspect':'Kiểm tra URI','connect.warning':'Đây là kết nối từ bên thứ ba, không thuộc mạng chính thức của GLO. GLO không xác nhận hoặc bảo đảm độ an toàn của provider. Chỉ sử dụng URI từ nguồn bạn tin tưởng.',
'settings.title':'Cài đặt','settings.general':'Chung','settings.language':'Ngôn ngữ','settings.performance':'Hiệu năng','settings.memory':'Tối ưu bộ nhớ cho GLO','settings.memory.desc':'Giải phóng bộ nhớ không cần thiết khi thu nhỏ GLO xuống system tray.','settings.memory.help':'Giải phóng WebView2 khi xuống tray. Kết nối mạng và Wintun vẫn hoạt động; mở lại giao diện có thể chậm hơn một chút.','settings.minimize':'Tối ưu bộ nhớ cho GLO khi thu nhỏ cửa sổ','settings.minimize.desc':'Áp dụng tối ưu bộ nhớ cả khi nhấn nút Minimize.','settings.minimize.help':'Khi bật, nhấn Minimize sẽ đưa GLO xuống system tray và giải phóng WebView2. Phiên kết nối vẫn chạy. Mặc định tắt.',
'settings.diagnostics':'Chẩn đoán','settings.debug':'Bật nhật ký gỡ lỗi','settings.debug.help':'Ghi thêm thông tin chẩn đoán để tìm lỗi; có thể làm tăng số lần ghi ổ đĩa.','settings.openLog':'Mở log',
'stats.ping':'RELAY PING','stats.loss':'PACKET LOSS','stats.left':'SESSION LEFT','stats.node':'NODE','chart.title':'Chất lượng kết nối','chart.period':'5 phút gần nhất','chart.ping':'Ping (ms)','chart.loss':'Mất gói (%)','chart.now':'Hiện tại','chart.seconds':'giây','chart.placeholder':'Thông tin về đường truyền sẽ hiển thị ở đây khi bạn bắt đầu gameplay','chart.waiting':'Đang chờ dữ liệu từ phiên chơi...','chart.collecting':'Đang thu thập dữ liệu đường truyền...',
'phase.waiting':'Đã kết nối · Chờ game','phase.standby':'Game đã mở · Chờ trận','phase.gameplay':'Gameplay','phase.connecting':'Đang kết nối','phase.disconnected':'Đã ngắt kết nối','phase.connected':'Đã kết nối',
'modal.close':'Đóng','modal.cancel':'Hủy','modal.accept':'Chấp nhận','modal.logout.title':'Đăng xuất khỏi GLO?','modal.logout.body':'Bạn sẽ cần mã đăng nhập mới để sử dụng mạng chính thức.','modal.logout.action':'Đăng xuất','modal.logout.active':'Bạn đang có phiên GLO hoạt động hoặc đang thiết lập kết nối. Đăng xuất sẽ ngắt phiên hiện tại.','modal.logout.activeAction':'Ngắt kết nối và đăng xuất','modal.logout.progress':'Đang ngắt kết nối và đăng xuất...','modal.logout.failed':'Không thể xác nhận đã ngắt phiên. Đăng xuất đã được hủy.','modal.disconnect.title':'Ngắt kết nối?','modal.disconnect.body':'Phiên GLO đang chạy sẽ bị dừng.','modal.disconnect.action':'Ngắt kết nối','modal.time.title':'Không đủ thời gian sử dụng','modal.time.body':'Bạn cần nhận thêm thời gian trên website GLO để tiếp tục sử dụng.','modal.time.action':'Nhận thêm thời gian','modal.provider.title':'Xác nhận provider','modal.provider.body':'Trò chơi: {game}\nNguồn: {source}. Chỉ kết nối nếu bạn tin tưởng provider này.','modal.expired.title':'Phiên kết nối đã hết hạn','modal.expired.body':'Thời gian sử dụng GLO đã hết. Hãy nhận thêm thời gian để tiếp tục.','modal.info':'Thông báo',
'error.invalidCode':'Mã đăng nhập gồm 10 ký tự.','error.invalidUri':'URI không hợp lệ','error.generic':'Có lỗi xảy ra','auth.startup':'Đang khởi tạo GLO...','auth.loading':'Đang tải giao diện WebView2...','auth.checking':'Đang khôi phục phiên đăng nhập...','auth.checking.desc':'Đang xác minh tài khoản và cập nhật dữ liệu.','auth.offline.title':'Không thể xác minh phiên đăng nhập','auth.offline.body':'Không thể kết nối tới GLO API. Thông tin đăng nhập đã lưu không bị xóa.','auth.retry':'Thử lại','connect.cardConnecting':'Đang kết nối...',
'connect.preparing':'Đang chuẩn bị phiên kết nối...','connect.verifying':'Đang kiểm tra relay và xác nhận kết nối...','connect.timeout':'Kết nối mất quá nhiều thời gian. Vui lòng thử lại.','account.signedOut':'Chưa đăng nhập'},
en:{
'account.logout':'Sign out','signin.title':'Sign in to GLO','signin.desc':'Get a one-time code from the website and enter it here. Each code is valid for three minutes.','signin.button':'Sign in','signin.website':'Get a code on the website ↗',
'wallet.label':'FREE TIME BANK','wallet.earn':'Earn time','wallet.minutes':'min','wallet.available':'{n} minutes available','wallet.reserved':'Available for new sessions: {n} min · In use: {r} min','wallet.sync':'Syncing with GLO...','wallet.updating':'Updating...','wallet.updateError':'Could not update; showing the previous balance.',
'services.title':'GLO Services','services.subtitle':'Select a game and server region to begin.','services.region':'Game server region','services.connect':'Connect','services.disconnect':'Disconnect','overlap.title':'Connection already active','overlap.body':'You are already optimizing {game} in {where}. Disconnect the current session before starting another connection.',
'connect.title':'Custom Connect','connect.subtitle':'Paste a provider-issued URI or open a glo://connect link from Windows.','connect.uri':'PROVIDER URI','connect.paste':'Paste from clipboard','connect.inspect':'Inspect URI','connect.warning':'This third-party connection is not part of the official GLO network. GLO does not certify or guarantee the provider’s security. Only use URIs from providers you trust.',
'settings.title':'Settings','settings.general':'General','settings.language':'Language','settings.performance':'Performance','settings.memory':'Optimize GLO memory','settings.memory.desc':'Release unnecessary memory when GLO is minimized to the system tray.','settings.memory.help':'Releases WebView2 when minimized to the tray. Wintun and the active network session keep running; reopening the UI may take longer.','settings.minimize':'Optimize memory when minimizing GLO','settings.minimize.desc':'Also release UI memory when pressing the Minimize button.','settings.minimize.help':'When enabled, Minimize sends GLO to the system tray and releases WebView2 while the network session continues. Off by default.',
'settings.diagnostics':'Diagnostics','settings.debug':'Enable debug logging','settings.debug.help':'Record extra diagnostic information to troubleshoot issues. This may increase disk writes.','settings.openLog':'Open log',
'stats.ping':'RELAY PING','stats.loss':'PACKET LOSS','stats.left':'SESSION LEFT','stats.node':'NODE','chart.title':'Network quality','chart.period':'Last 5 minutes','chart.ping':'Ping (ms)','chart.loss':'Packet loss (%)','chart.now':'Now','chart.seconds':'seconds','chart.placeholder':'Connection quality information will appear here when you start gameplay','chart.waiting':'Waiting for gameplay data...','chart.collecting':'Collecting connection quality data...',
'art.help':'Advanced Routing connects through multiple relay nodes. The Time-Factor is provided by GLO and multiplies your Time Bank usage.','art.label':'Advanced Routing','stats.route':'ROUTE','phase.waiting':'Connected · Waiting for game','phase.standby':'Game detected · Waiting for match','phase.gameplay':'Gameplay','phase.connecting':'Connecting','phase.disconnected':'Disconnected','phase.connected':'Connected',
'modal.close':'Close','modal.cancel':'Cancel','modal.accept':'Accept','modal.logout.title':'Sign out of GLO?','modal.logout.body':'You will need a new login code to access the official network.','modal.logout.action':'Sign out','modal.logout.active':'A GLO session is active or connecting. Signing out will disconnect your current session.','modal.logout.activeAction':'Disconnect and sign out','modal.logout.progress':'Disconnecting and signing out...','modal.logout.failed':'Could not confirm disconnection. Sign-out was cancelled.','modal.disconnect.title':'Disconnect?','modal.disconnect.body':'Your active GLO session will end.','modal.disconnect.action':'Disconnect','modal.time.title':'Not enough time','modal.time.body':'Earn more time on the GLO website to continue.','modal.time.action':'Earn more time','modal.provider.title':'Confirm provider','modal.provider.body':'Game: {game}\nSource: {source}. Only connect if you trust this provider.','modal.expired.title':'Session expired','modal.expired.body':'Your GLO time has run out. Earn more time to continue.','modal.info':'Notice',
'error.invalidCode':'Login codes must contain 10 characters.','error.invalidUri':'Invalid URI','error.generic':'Something went wrong','auth.startup':'Starting GLO...','auth.loading':'Loading WebView2 interface...','auth.checking':'Restoring your sign-in session...','auth.checking.desc':'Verifying your account and refreshing data.','auth.offline.title':'Could not verify your sign-in session','auth.offline.body':'GLO API is unavailable. Your saved credentials were not deleted.','auth.retry':'Retry','connect.cardConnecting':'Connecting...',
'connect.preparing':'Preparing connection...','connect.verifying':'Verifying relay and completing connection...','connect.timeout':'Connection timed out. Please try again.','account.signedOut':'Not signed in'}};
Object.assign(translations.vi,{'connect.inspecting':'Đang xác minh liên kết từ provider...', 'connect.probing':'Đang đo độ trễ các relay...', 'connect.redeeming':'Đang xác nhận phiên với provider...', 'connect.starting':'Đang khởi tạo kết nối mạng...'});
Object.assign(translations.vi,{
'api.error.NETWORK_TIMEOUT':'Kết nối tới GLO API đã hết thời gian chờ.',
'api.error.NETWORK_UNAVAILABLE':'Không liên lạc được với GLO API. Vui lòng kiểm tra Internet.',
'api.error.AUTH_REQUIRED':'Phiên đăng nhập đã hết hạn. Vui lòng đăng nhập lại.',
'api.error.RATE_LIMITED':'Bạn thao tác quá nhanh. Vui lòng thử lại sau.',
'api.error.SERVICE_MAINTENANCE':'GLO Services đang tạm bảo trì.',
'api.error.PROFILE_UNAVAILABLE':'Game hoặc khu vực máy chủ hiện không khả dụng.',
'api.error.HANDOFF_CANDIDATES_UNAVAILABLE':'Hiện không có relay khả dụng để thiết lập phiên.',
'api.error.ART_NO_ELIGIBLE_ROUTE':'Không tìm thấy tuyến Định tuyến nâng cao ONLINE hỗ trợ trò chơi này. Time Bank được bảo toàn.',
'api.error.ART_ADMISSION_FAILED':'Không thể thiết lập tuyến Định tuyến nâng cao. Nếu phiên chưa bắt đầu, thời gian tạm giữ sẽ được hoàn lại.',
'api.error.ART_NO_REACHABLE_ROUTE':'Không có tuyến Định tuyến nâng cao phản hồi khi đo. Không phát sinh phiên mới.',
'api.error.SERVER_ERROR':'Máy chủ GLO API đang gặp lỗi.',
'api.error.PREPARE_FAILED':'Không thể chuẩn bị phiên GLO. Vui lòng thử lại.',
'api.error.MISSING_HANDOFF_TOKEN':'Phản hồi từ provider thiếu thông tin phiên.',
'api.error.NO_REACHABLE_RELAY':'Không có relay nào phản hồi khi đo.',
'api.error.INVALID_ISSUER':'Danh tính provider không hợp lệ.',
'api.error.PROVIDER_TIMEOUT':'Provider không phản hồi kịp thời.',
'api.error.HANDOFF_FAILED':'Không thể hoàn tất xác nhận phiên với provider.'
});
Object.assign(translations.en,{
'api.error.NETWORK_TIMEOUT':'Connection to GLO API timed out.',
'api.error.NETWORK_UNAVAILABLE':'Could not reach GLO API. Check your Internet connection.',
'api.error.AUTH_REQUIRED':'Sign-in session expired. Please sign in again.',
'api.error.RATE_LIMITED':'Too many requests. Please try again later.',
'api.error.SERVICE_MAINTENANCE':'GLO Services is temporarily under maintenance.',
'api.error.PROFILE_UNAVAILABLE':'Selected game or region is unavailable.',
'api.error.HANDOFF_CANDIDATES_UNAVAILABLE':'No relay candidates are available.',
'api.error.ART_NO_ELIGIBLE_ROUTE':'No eligible Advanced Routing path supports this game. Time Bank is preserved.',
'api.error.ART_ADMISSION_FAILED':'Unable to establish Advanced Routing. If no session started, reserved time will be released.',
'api.error.ART_NO_REACHABLE_ROUTE':'No Advanced Routing candidate responded. No session was established.',
'api.error.SERVER_ERROR':'GLO API returned a server error.',
'api.error.PREPARE_FAILED':'Unable to prepare a GLO session. Please retry.',
'api.error.MISSING_HANDOFF_TOKEN':'Provider response is missing session information.',
'api.error.NO_REACHABLE_RELAY':'No relay responded to measurements.',
'api.error.INVALID_ISSUER':'Invalid provider identity.',
'api.error.PROVIDER_TIMEOUT':'Provider did not respond in time.',
'api.error.HANDOFF_FAILED':'Could not finish provider handoff.'
});
Object.assign(translations.en,{'connect.inspecting':'Inspecting provider link...', 'connect.probing':'Measuring relay latency...', 'connect.redeeming':'Redeeming provider session...', 'connect.starting':'Starting network connection...'});
function t(key,args={}){return (translations[state.language]?.[key]||translations.vi[key]||key).replace(/\{(\w+)\}/g,(_,name)=>String(args[name]??''));}
function send(action,data={}){bridge?.postMessage({action,...data});}
function show(id,visible){$(id)?.classList.toggle('hidden',!visible);}
function translate(){document.documentElement.lang=state.language;document.querySelectorAll('[data-i18n]').forEach(el=>{el.textContent=t(el.dataset.i18n);});document.querySelectorAll('[data-tip]').forEach(el=>el.setAttribute('aria-label',t(el.dataset.tip)));$('account-btn').title=state.language==='vi'?'Tài khoản':'Account';$('settings-btn').title=t('settings.title');$('theme-btn').title=state.language==='vi'?'Sáng/Tối':'Light/Dark';}
function toast(message){$('toast').textContent=message;show('toast',true);clearTimeout(toast.timer);toast.timer=setTimeout(()=>show('toast',false),3000);}
function modal(title,message,buttons=[{label:t('modal.close'),action:'close',kind:'outline'}]){state.modal={title,message,buttons};$('modal-title').textContent=title;$('modal-text').textContent=message;const box=$('modal-actions');box.replaceChildren();for(const b of buttons){const button=document.createElement('button');button.textContent=b.label;button.className=b.kind||'primary';button.addEventListener('click',()=>{show('modal-overlay',false);state.modal=null;if(b.action==='earn')send('open.time');if(b.action==='custom.confirm'){beginConnecting('verifying','connect');state.waiting=true;send('custom.confirm');}if(b.action==='custom.cancel')send('custom.cancel');if(b.action==='disconnect'){state.waiting=true;send('disconnect');}if(b.action==='logout'){state.logoutPending=true;state.waiting=true;send('auth.logout');}render();});box.append(button);}show('modal-overlay',true);}
function resetConnecting(){state.pendingGame="";state.waiting=false;state.connectStage='';state.connectSource='';state.connectSawWorker=false;state.connectAttempt++;}
function beginConnecting(stage,source){state.connectAttempt++;state.connectStage=stage;state.connectSource=source;state.connectSawWorker=false;}
function connectLocked(){return !!(state.logoutPending||state.waiting||state.connectStage||state.connection?.active||state.connection?.phase==='connecting'||state.connection?.disconnecting);}
function closeTimeBank(){show('timebank-menu',false);$('timebank-btn').setAttribute('aria-expanded','false');}
function closeAccount(){show('account-menu',false);}
function closeTopMenus(){closeTimeBank();closeAccount();}
function saveView(){send('ui.state',{mode:state.mode,game:state.game,region:state.region});}
function setMode(mode){if(!['services','connect'].includes(mode)||connectLocked())return;state.mode=mode;state.page=mode;saveView();show('mode-menu',false);$('mode-btn').setAttribute('aria-expanded','false');closeTopMenus();render();}
function isRelay(c=state.connection){return !!c?.active&&c.phase==='gameplay'&&c.route==='RELAY'&&Number.isFinite(c.ping_ms)&&Number.isFinite(c.loss_pct);}
function resetGraph(){state.history=[];state.graphEpoch=null;show('quality-chart-card',false);const canvas=$('quality-chart');const ctx=canvas.getContext('2d');if(ctx)ctx.clearRect(0,0,canvas.width,canvas.height);}
function chartVisible(){return state.page!== 'settings'&&document.visibilityState==='visible'&&isRelay();}
function renderGraph(){const card=$('quality-chart-card');const should=chartVisible()&&state.history.length>0;show('quality-chart-card',should);if(!should)return;const canvas=$('quality-chart');const rect=canvas.getBoundingClientRect();if(rect.width<40)return;const dpr=Math.min(2,window.devicePixelRatio||1),w=rect.width,h=220;canvas.width=Math.round(w*dpr);canvas.height=Math.round(h*dpr);const ctx=canvas.getContext('2d');ctx.setTransform(dpr,0,0,dpr,0,0);ctx.clearRect(0,0,w,h);const css=getComputedStyle(document.body),border=css.getPropertyValue('--border').trim(),fg=css.getPropertyValue('--muted').trim(),pingColor=css.getPropertyValue('--accent-text').trim();const left=45,right=w-48,top=14,bottom=h-34;const points=state.history.filter(p=>p&&Number.isFinite(p.ping)&&Number.isFinite(p.loss)&&Number.isFinite(p.age)&&p.age>=0&&p.age<=300);if(!points.length){show('quality-chart-card',false);return;}const maxPing=Math.max(30,Math.ceil(Math.max(...points.map(p=>p.ping))*1.2/10)*10),maxLoss=Math.max(1,Math.ceil(Math.max(...points.map(p=>p.loss))*1.2));ctx.font='10px Segoe UI,Arial';ctx.strokeStyle=border;ctx.fillStyle=fg;ctx.lineWidth=1;for(let tick=0;tick<=10;tick++){const x=left+(right-left)*tick/10;ctx.beginPath();ctx.moveTo(x,top);ctx.lineTo(x,bottom);ctx.stroke();ctx.textAlign='center';ctx.fillText(tick===10?t('chart.now'):`-${Math.round((10-tick)*.5*10)/10}m`,x,h-11);}for(let i=0;i<=4;i++){const y=top+(bottom-top)*i/4;ctx.beginPath();ctx.moveTo(left,y);ctx.lineTo(right,y);ctx.stroke();ctx.textAlign='right';ctx.fillText(String(Math.round(maxPing*(1-i/4))),left-6,y+3);ctx.textAlign='left';ctx.fillText(String(Number((maxLoss*(1-i/4)).toFixed(1))),right+5,y+3);}function draw(key,limit,color){ctx.beginPath();ctx.strokeStyle=color;ctx.lineWidth=2;points.forEach((p,i)=>{const x=right-(right-left)*p.age/300,y=bottom-(bottom-top)*Math.max(0,Math.min(1,p[key]/limit));if(i===0)ctx.moveTo(x,y);else ctx.lineTo(x,y)});ctx.stroke();}draw('ping',maxPing,pingColor);draw('loss',maxLoss,'#a98cff');}
let catalogRendered={catalog:null,regions:null,game:null,language:null};
function renderCatalog(){const catalog=state.catalog;if(catalogRendered.catalog===state.catalog&&catalogRendered.regions===state.regions&&catalogRendered.game===state.game&&catalogRendered.language===state.language)return;catalogRendered={catalog:state.catalog,regions:state.regions,game:state.game,language:state.language};for(const card of document.querySelectorAll('.game-card')){const found=catalog.find(g=>g.id===card.dataset.game||g.game===card.dataset.game);card.classList.toggle('hidden',catalog.length>0&&!found);}const available=state.regions.filter(r=>r.game===state.game).map(r=>r.region);const values=available.map(r=>typeof r==='string'?r:r.code||r.id||r.region).filter(Boolean);const select=$('region'),hash=JSON.stringify(values);if(values.length&&select.dataset.keys!==hash){select.replaceChildren();for(const id of values){const o=document.createElement('option');o.value=id;o.textContent=id==='SG'?'Singapore (SG)':id==='HK'?'Hong Kong (HK)':id;select.append(o);}select.dataset.keys=hash;}if(values.length&&!values.includes(state.region))state.region=values[0];select.value=state.region;}
function updateText(id,value){const el=$(id);if(el.textContent!==value)el.textContent=value;}
function renderStats(){const c=state.connection;if(!c){show('quality-placeholder',false);return;}const quality=isRelay(c);$('service-stats').classList.toggle('with-quality',quality);show('art-session-pill',!!c.art_enabled);updateText('art-session-pill',state.language==='vi'?'Định tuyến nâng cao':'Advanced Routing');updateText('stat-node-label',c.art_enabled?t('stats.route'):t('stats.node'));updateText('stat-route',c.route||'WAITING');updateText('stat-message',c.message||'');const ping=c.ping_ms==null?'—':`${Math.round(c.ping_ms)} ms`,loss=c.loss_pct==null?'—':`${Number(c.loss_pct).toFixed(1)}%`;updateText('stat-ping',ping);updateText('stat-loss',loss);updateText('stat-left',c.left||'—');updateText('stat-node',c.node||'—');if(!isRelay(c)){resetGraph();}else{renderGraph();}
 const chartHasSamples=chartVisible()&&state.history.length>0;
 const present=!!c.active&&c.phase!=='connecting'&&state.page!=='settings';
 show('quality-placeholder',present&&!chartHasSamples);
 if(present&&!chartHasSamples){
   updateText('quality-placeholder-text',t('chart.placeholder'));
   updateText('quality-placeholder-subtext',t(c.phase==='gameplay'?'chart.collecting':'chart.waiting'));
 }
}
let lastTranslatedLanguage='';
function render(){
const active=!!state.connection?.active&&state.connection?.phase!=='connecting';
const locked=connectLocked();
document.body.classList.toggle('settings-layout',state.page==='settings');
document.body.classList.toggle('connect-locked',locked);
$('mode-btn').disabled=locked;
$('mode-btn').setAttribute('aria-disabled',String(locked));
$('mode-btn').tabIndex=locked?-1:0;
document.body.classList.toggle('mode-locked',locked);
if(locked){show('mode-menu',false);$('mode-btn').setAttribute('aria-expanded','false');} 
const authBlocked=state.mode==='services'&&state.page==='services'&&!active;
const boot=!state.initialized||(authBlocked&&state.authStatus==='checking'&&!state.logoutPending);
const offline=authBlocked&&state.initialized&&state.authStatus==='offline'&&!state.logoutPending;
show('startup-loading',boot);show('auth-offline',offline);
$('startup-title').textContent=t(state.initialized?'auth.checking':'auth.startup');
$('startup-detail').textContent=t(state.initialized?'auth.checking.desc':'auth.loading');
for(const page of ['services','connect','settings'])show(page,state.page===page&&!boot&&!offline);
show('account-btn',state.mode==='services'&&state.logged&&!state.logoutPending);show('timebank-wrap',state.mode==='services'&&state.logged&&!state.logoutPending);if(state.mode!=='services'||!state.logged||state.logoutPending)closeTopMenus();$('mode-text').textContent=state.mode==='services'?'Services':'Connect';show('signin',state.authStatus==='unauthenticated'&&!active&&!connectLocked());show('service-authenticated',state.logged||(active&&state.connection?.source!=='custom')||state.logoutPending&&active);show('account-logout',state.logged);$('account-email').textContent=state.account||t('account.signedOut');document.body.dataset.theme=state.theme;$('wordmark').src=state.theme==='dark'?'assets/wordmark-light.png':'assets/wordmark-dark.png';$('theme-use').setAttribute('href',state.theme==='dark'?'#i-sun':'#i-moon');const c=state.connection;show('service-picker',!active);$('art-enabled').checked=state.artEnabled;$('art-enabled').disabled=connectLocked()||state.artFactor==null;updateText('art-label',t('art.label')+' ('+(state.artFactor==null?'—':Number(state.artFactor).toLocaleString(state.language==='vi'?'vi-VN':'en-US'))+'×)');const destination=(state.page==='connect'||(state.page==='settings'&&state.mode==='connect'))?$('connect'):$('service-authenticated');if($('service-stats').parentNode!==destination)destination.appendChild($('service-stats'));show('service-stats',active);
show('logout-loading',state.logoutPending);
if(state.logoutPending)$('logout-loading-text').textContent=t('modal.logout.progress');
$('service-authenticated').classList.toggle('session-live',active);
show('connect-progress',!!state.connectStage&&!active&&state.page===state.connectSource);
$('connect-progress-text').textContent=t('connect.'+( ['preparing','inspecting','verifying','probing','redeeming','starting'].includes(state.connectStage)?state.connectStage:'preparing'));
for(const card of document.querySelectorAll('.game-card'))card.classList.toggle('is-connecting',!active&&state.connectSource==='services'&&!!state.connectStage&&card.dataset.game===(state.pendingGame||state.game));
show('custom-connect-form',!active);
$('inspect-uri').disabled=locked;
$('paste-uri').disabled=locked;
const gameId=String(active?(c?.game||''):'').toLowerCase();const images={roblox:'roblox-presskit.jpg',valorant:'valorant-presskit.jpg',pubg:'pubg-presskit.jpg',tft:'tft-presskit.jpg'};const artwork=images[gameId];const displayGames={tft:'Teamfight Tactics',pubg:'PUBG: BATTLEGROUNDS',valorant:'VALORANT',roblox:'Roblox'};const nextArtwork=artwork?'assets/'+artwork:'assets/custom-game-placeholder.png';if(!$('active-game-image').getAttribute('src')||!$('active-game-image').getAttribute('src').endsWith(nextArtwork))$('active-game-image').src=nextArtwork;$('active-game-image').alt=artwork?gameId:'Custom game';$('active-game-image').classList.toggle('placeholder-image',!artwork);$('active-game-name').textContent=artwork?({pubg:'PUBG: BATTLEGROUNDS',valorant:'VALORANT',roblox:'Roblox',tft:'Teamfight Tactics'}[gameId]):(displayGames[gameId]||c?.game||'Custom Game');$('active-game-region').textContent=c?.source==='custom'?'Custom Provider':c?.region==='SG'?'Singapore (SG)':c?.region==='HK'?'Hong Kong (HK)':(c?.region||'—');show('node-eye',active&&c?.source==='custom');$('node-eye-use').setAttribute('href',c?.node_visible===false?'#i-eye-off':'#i-eye');for(const card of document.querySelectorAll('.game-card')){card.classList.toggle('locked',active||locked);card.querySelector('button').disabled=active||locked;}const w=state.wallet;if(w){const cap=w.max_balance_seconds||0,rem=w.remaining_seconds||0,res=w.reserved_seconds||0;const total=rem+res;$('wallet-value').textContent=`(${Math.floor(total/60)} / ${Math.floor(cap/60)} ${t('wallet.minutes')})`; $('wallet-progress').setAttribute('aria-valuenow',String(Math.max(0,Math.min(100,Math.round(cap?total/cap*100:0)))));$('wallet-bar').style.width=`${Math.max(0,Math.min(100,cap?total/cap*100:0))}%`;$('wallet-hint').textContent=res?t('wallet.reserved',{n:Math.floor(rem/60),r:Math.floor(res/60)}):t('wallet.available',{n:Math.floor(rem/60)});}else{$('wallet-value').textContent=`(— / — ${t('wallet.minutes')})`; $('wallet-progress').setAttribute('aria-valuenow','0');$('wallet-bar').style.width='0%';$('wallet-hint').textContent=t('wallet.sync');}const walletStatus=$('wallet-sync-status');
 const statusMessage=state.walletStatus==='loading'?t('wallet.updating'):state.walletStatus==='error'?t('wallet.updateError'):'';
 updateText('wallet-sync-status',statusMessage);show('wallet-sync-status',!!statusMessage);
 walletStatus.classList.toggle('wallet-status-error',state.walletStatus==='error');
 show('view-debug',$('debug-log').checked);show('minimize-opt-row',state.optimizeMemory);$('optimize-on-minimize').checked=state.optimizeOnMinimize;$('optimize-memory').checked=state.optimizeMemory;if(lastTranslatedLanguage!==state.language){translate();lastTranslatedLanguage=state.language;}
// Reconcile card captions after translation and after every authoritative session snapshot.
for(const card of document.querySelectorAll('.game-card')){const pending=!active&&state.connectSource==='services'&&!!state.connectStage&&card.dataset.game===(state.pendingGame||state.game);const btn=card.querySelector('.game-connect');const label=t(pending?'connect.cardConnecting':'services.connect');if(btn.textContent!==label)btn.textContent=label;}
renderCatalog();renderStats();if(!chartVisible())show('quality-chart-card',false);}
function receive(data){if(!data||typeof data!=='object')return;switch(data.type){case 'init':state.theme=data.theme==='light'?'light':'dark';state.language=data.language==='en'?'en':'vi';$('language').value=state.language;state.initialized=true;state.authStatus='checking';$('debug-log').checked=!!data.debug;state.optimizeMemory=data.optimize_memory!==false;state.optimizeOnMinimize=!!data.optimize_on_minimize;state.mode=data.mode==='connect'?'connect':'services';state.page=state.mode;state.game=['roblox','valorant','pubg','tft'].includes(data.game)?data.game:'valorant';state.region=typeof data.region==='string'?data.region:'SG';
// IsVisible(FALSE) may suspend requestAnimationFrame; a DOM-ready timer still runs.
setTimeout(()=>send('ui.rendered'),0);break;case 'mode':if(data.mode==='connect'||data.mode==='services')setMode(data.mode);break;case 'auth':
 if(data.status==='offline'){if(!state.logoutPending&&!state.logged){state.authStatus='offline';state.loginPending=false;}break;}
 if(state.logoutPending&&data.logged!==false)break;
 if(state.logoutPending&&data.logged===false){state.logoutPending=false;resetConnecting();resetGraph();state.connection=null;}
 state.logged=!!data.logged;state.authStatus=state.logged?'authenticated':'unauthenticated';state.loginPending=false;
 state.account=data.email||'';if(!state.logged){state.artEnabled=false;state.artFactor=null;state.wallet=null;state.walletStatus='idle';state.catalog=[];closeTimeBank();}break;
case 'clipboard-text':if(pendingPaste){const {el,start,end}=pendingPaste;pendingPaste=null;if(el.isConnected&&!el.disabled&&!el.readOnly&&typeof data.text==='string'){el.focus();el.setRangeText(data.text,start,end,'end');el.dispatchEvent(new Event('input',{bubbles:true}));}}break;case 'clipboard-written':if(pendingCut){const {el,start,end,text}=pendingCut;pendingCut=null;if(data.ok&&el.isConnected&&!el.disabled&&!el.readOnly&&el.value.slice(start,end)===text){el.focus();el.setRangeText('',start,end,'start');el.dispatchEvent(new Event('input',{bubbles:true}));}}break;case 'logout-progress':state.logoutPending=true;state.waiting=true;break;case 'logout-aborted':state.logoutPending=false;state.waiting=false;toast(t('modal.logout.failed'));break;case 'connect-progress':{if(state.logoutPending)break;const source=data.source==='services'?'services':'connect';const stage=['inspecting','verifying','probing','redeeming','starting'].includes(data.stage)?data.stage:'preparing';
if(state.connectSource===source&&state.connectStage)state.connectStage=stage;else beginConnecting(stage,source);state.waiting=true;break;}case 'catalog':if(state.logoutPending)break;state.artFactor=(data.art_time_factor!=null&&Number.isFinite(Number(data.art_time_factor))&&Number(data.art_time_factor)>=1&&Number(data.art_time_factor)<=10)?Number(data.art_time_factor):null;if(state.artFactor==null)state.artEnabled=false;state.catalog=Array.isArray(data.games)?data.games:[];state.regions=Array.isArray(data.regions)?data.regions:[];break;case 'wallet':if(state.logoutPending)break;state.wallet=data.wallet||null;state.walletStatus='idle';break;case 'wallet-status':if(state.logoutPending)break;state.walletStatus=data.status==='loading'?'loading':data.status==='error'?'error':'idle';break;case 'connection':{const previous=isRelay();state.connection=data.connection||null;if(data.connection?.active&&data.connection?.game){state.activeGame=data.connection.game;}else if(!data.connection?.active){state.activeGame='';state.activeRegion='';}
if(data.connection?.active&&data.connection?.region)state.activeRegion=data.connection.region;
state.waiting=state.logoutPending||!!data.waiting||!!data.connection?.disconnecting;if(state.connection?.active){state.connectSawWorker=true;
if(state.connection.phase==='connecting'&&!state.connectStage)beginConnecting('starting',state.connection.source==='official'?'services':'connect');
if(state.connection.phase!=='connecting'&&!state.connection.disconnecting)resetConnecting();}else if(!state.waiting&&state.connectStage&&state.connection?.phase==='disconnected')resetConnecting();if(previous&&!isRelay())resetGraph();if(Array.isArray(data.history))state.history=data.history.filter(p=>Number.isFinite(p.age)&&Number.isFinite(p.ping)&&Number.isFinite(p.loss));break;}case 'toast':toast(data.message||'');break;case 'error':if(state.logoutPending)break;resetConnecting();if(state.loginPending){state.loginPending=false;state.authStatus='unauthenticated';}toast(data.error_code&&translations[state.language]?.['api.error.'+data.error_code]?t('api.error.'+data.error_code):(data.message||t('error.generic')));break;case 'modal':modal(data.title||t('modal.info'),data.message||'',data.buttons||undefined);break;case 'need-time':resetConnecting();modal(t('modal.time.title'),data.message||t('modal.time.body'),[{label:t('modal.close'),action:'close',kind:'outline'},{label:t('modal.time.action'),action:'earn'}]);break;case 'overlap':if(!state.connection?.active)resetConnecting();modal(t('overlap.title'),t('overlap.body',{game:data.game||'—',where:data.where==='connect'?'Connect':'Services'}));break;case 'custom-offer':resetConnecting();modal(t('modal.provider.title'),t('modal.provider.body',{game:data.game||'—',source:data.source||'—'}),[{label:t('modal.cancel'),action:'custom.cancel',kind:'outline'},{label:t('modal.accept'),action:'custom.confirm'}]);break;case 'expired':resetConnecting();resetGraph();modal(t('modal.expired.title'),t('modal.expired.body'),[{label:t('modal.close'),action:'close',kind:'outline'},{label:t('modal.time.action'),action:'earn'}]);break;}render();}
bridge?.addEventListener('message',e=>receive(e.data));
$('mode-btn').addEventListener('click',()=>{if(connectLocked())return;show('mode-menu',$('mode-menu').classList.contains('hidden'));$('mode-btn').setAttribute('aria-expanded',String(!$('mode-menu').classList.contains('hidden')));});
document.querySelectorAll('[data-mode]').forEach(el=>el.addEventListener('click',()=>setMode(el.dataset.mode)));
$('account-btn').addEventListener('click',()=>{if(state.logoutPending||!state.logged)return;closeTimeBank();show('account-menu',$('account-menu').classList.contains('hidden'));});
$('timebank-btn').addEventListener('click',()=>{if(!state.logged||state.mode!=='services'||state.logoutPending)return;closeAccount();const expanded=$('timebank-menu').classList.contains('hidden');show('timebank-menu',expanded);$('timebank-btn').setAttribute('aria-expanded',String(expanded));if(expanded){if(!state.wallet)state.walletStatus='loading';send('wallet.refresh');render();}});
function markSelectedTimeBank(id){state.selectedTimeBank=id;for(const choice of document.querySelectorAll('#timebank-choices [role=radio]'))choice.setAttribute('aria-checked',String(choice.closest('[data-bank]')?.dataset.bank===id));}
$('free-bank-choice').addEventListener('click',()=>{if(connectLocked())return;markSelectedTimeBank('free');});
// Do not add client-only Premium selection: future banks must be confirmed by the
// provider before calling markSelectedTimeBank. One bank at a time (radio group).
$('account-logout').addEventListener('click',()=>{closeTopMenus();modal(t('modal.logout.title'),connectLocked()?t('modal.logout.active'):t('modal.logout.body'),[{label:t('modal.cancel'),action:'close',kind:'outline'},{label:connectLocked()?t('modal.logout.activeAction'):t('modal.logout.action'),action:'logout'}]);});
$('settings-btn').addEventListener('click',()=>{if(state.logoutPending)return;closeTopMenus();state.page=state.page==='settings'?state.mode:'settings';render();});
$('settings-back').addEventListener('click',()=>{state.page=state.mode;render();});
$('theme-btn').addEventListener('click',()=>{closeTopMenus();state.theme=state.theme==='dark'?'light':'dark';send('settings.theme',{theme:state.theme});render();});
$('language').addEventListener('change',()=>{state.language=$('language').value==='en'?'en':'vi';send('settings.language',{language:state.language});render();});
$('debug-log').addEventListener('change',()=>{send('settings.debug',{enabled:$('debug-log').checked});render();});$('view-debug').addEventListener('click',()=>send('settings.log'));
$('optimize-memory').addEventListener('change',()=>{state.optimizeMemory=$('optimize-memory').checked;send('settings.optimizeMemory',{enabled:state.optimizeMemory});render();});
$('optimize-on-minimize').addEventListener('change',()=>{state.optimizeOnMinimize=$('optimize-on-minimize').checked;send('settings.optimizeOnMinimize',{enabled:state.optimizeOnMinimize});render();});
$('auth-retry').addEventListener('click',()=>{state.authStatus='checking';send('auth.retry');render();});
$('login-web').addEventListener('click',()=>send('open.login'));
$('login-submit').addEventListener('click',()=>{const code=$('login-code').value.trim();if(!/^[A-HJ-NP-Z2-9]{5}-?[A-HJ-NP-Z2-9]{5}$/i.test(code)){toast(t('error.invalidCode'));return;}state.loginPending=true;state.authStatus='checking';send('auth.redeem',{code});render();$('login-submit').disabled=true;setTimeout(()=>$('login-submit').disabled=false,1200);});
$('earn-time').addEventListener('click',()=>{closeTimeBank();send('open.time');});
$('art-enabled').addEventListener('change',()=>{if(connectLocked()){$('art-enabled').checked=state.artEnabled;return;}state.artEnabled=state.artFactor!=null&&$('art-enabled').checked;render();});
$('region').addEventListener('change',()=>{state.region=$('region').value;saveView();});
function connectGame(game){if(connectLocked()||state.connection?.active||!state.logged)return;state.pendingGame=game;state.game=game;state.region=$('region').value;saveView();state.waiting=true;beginConnecting('preparing','services');send('services.connect',{game,region:state.region,routing_preference:state.artEnabled?'advanced':'standard'});render();
// Native connection state is authoritative: never reset a live worker on a JS timer.
}
for(const card of document.querySelectorAll('.game-card')){card.querySelector('.game-connect').addEventListener('click',e=>{e.stopPropagation();connectGame(card.dataset.game);});}
$('service-disconnect').addEventListener('click',()=>modal(t('modal.disconnect.title'),t('modal.disconnect.body'),[{label:t('modal.cancel'),action:'close',kind:'outline'},{label:t('modal.disconnect.action'),action:'disconnect'}]));
$('node-eye').addEventListener('click',()=>send('custom.toggleIp'));
$('inspect-uri').addEventListener('click',()=>{if(connectLocked()||state.connection?.active)return;const uri=$('custom-uri').value.trim();if(!uri.startsWith('glo://connect')){toast(t('error.invalidUri'));return;}beginConnecting('inspecting','connect');state.waiting=true;send('custom.inspect',{uri});render();});$('paste-uri').addEventListener('click',()=>{if(connectLocked()||state.connection?.active)return;beginConnecting('inspecting','connect');state.waiting=true;send('custom.paste');render();});
document.querySelectorAll('[data-open]').forEach(el=>el.addEventListener('click',()=>send('open.external',{url:el.dataset.open})));
function tooltipFor(button){const tip=$('tip-overlay');tip.textContent=t(button.dataset.tip);show('tip-overlay',true);const r=button.getBoundingClientRect(),box=tip.getBoundingClientRect();tip.style.left=`${Math.max(8,Math.min(innerWidth-box.width-8,r.left-box.width/2+8))}px`;tip.style.top=`${r.top>box.height+15?r.top-box.height-9:r.bottom+9}px`;}
document.querySelectorAll('[data-tip]').forEach(el=>{el.addEventListener('mouseenter',()=>tooltipFor(el));el.addEventListener('focus',()=>tooltipFor(el));el.addEventListener('mouseleave',()=>show('tip-overlay',false));el.addEventListener('blur',()=>show('tip-overlay',false));});
document.addEventListener('click',e=>{if(!e.target.closest('.mode-wrap')){show('mode-menu',false);$('mode-btn').setAttribute('aria-expanded','false');}if(!e.target.closest('#account-btn')&&!e.target.closest('#account-menu'))closeAccount();if(!e.target.closest('#timebank-wrap'))closeTimeBank();});
document.addEventListener('keydown',e=>{if(e.key==='Escape')closeTopMenus();});
window.addEventListener('resize',()=>{if(chartVisible())renderGraph();});
document.addEventListener('visibilitychange',()=>{if(document.visibilityState==='visible')renderGraph();else show('quality-chart-card',false);});

// Desktop input context menu: no browser context menus or page text selection.
const inputMenu=$('input-context-menu');
let inputSelection=null,pendingCut=null,pendingPaste=null;
function textField(node){return node?.closest?.('input:not([type=checkbox]):not([type=radio]),textarea')||null;}
function closeInputMenu(){show('input-context-menu',false);inputSelection=null;}
function selectionValid(info){return info&&info.el.isConnected&&!info.el.disabled&&!info.el.readOnly;}
function openInputMenu(el,x,y){
  const start=el.selectionStart??0,end=el.selectionEnd??start;
  inputSelection={el,start,end};
  inputMenu.querySelector('[data-edit=copy]').disabled=start===end;
  inputMenu.querySelector('[data-edit=cut]').disabled=start===end||el.readOnly||el.disabled;
  inputMenu.querySelector('[data-edit=paste]').disabled=el.readOnly||el.disabled;
  inputMenu.querySelector('[data-edit=select-all]').disabled=el.disabled;
  show('input-context-menu',true);
  inputMenu.style.left=`${Math.max(8,Math.min(x,innerWidth-inputMenu.offsetWidth-8))}px`;
  inputMenu.style.top=`${Math.max(8,Math.min(y,innerHeight-inputMenu.offsetHeight-8))}px`;
}
document.addEventListener('contextmenu',e=>{e.preventDefault();const el=textField(e.target);if(el)openInputMenu(el,e.clientX,e.clientY);else closeInputMenu();});
inputMenu.addEventListener('mousedown',e=>e.preventDefault());
inputMenu.addEventListener('click',e=>{
  const item=e.target.closest('[data-edit]'),info=inputSelection;if(!item||item.disabled||!info)return;
  const {el,start,end}=info,cmd=item.dataset.edit;
  closeInputMenu();el.focus();el.setSelectionRange(start,end);
  if(cmd==='select-all'){el.select();return;}
  if(cmd==='paste'){pendingPaste={el,start,end};send('input.paste');return;}
  if(cmd==='copy'||cmd==='cut'){
    const selected=el.value.slice(start,end);if(!selected)return;
    pendingCut=cmd==='cut'&&selectionValid(info)?{el,start,end,text:selected}:null;
    send('input.write',{text:selected});
  }
});
document.addEventListener('keydown',e=>{
  if(e.key==='Escape'){closeInputMenu();return;}
  const input=textField(e.target);
  if(!input&&(e.ctrlKey||e.metaKey)&&['a','c','x','v'].includes(e.key.toLowerCase()))e.preventDefault();
  if(input&&e.shiftKey&&e.key==='F10'){e.preventDefault();const r=input.getBoundingClientRect();openInputMenu(input,r.left+12,r.bottom-6);}
});
document.addEventListener('mousedown',e=>{if(!e.target.closest('#input-context-menu'))closeInputMenu();});
document.addEventListener('dragstart',e=>{if(!textField(e.target))e.preventDefault();});
document.querySelectorAll('img').forEach(img=>img.draggable=false);

window.gloReceive=receive;send('ui.ready');render();
