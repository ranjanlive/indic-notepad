"""Deterministic vector/product icon exports; Pillow is a build-time tool only."""
from pathlib import Path
from PIL import Image, ImageDraw, ImageFont
ROOT=Path(__file__).resolve().parents[1]
svg='''<svg xmlns="http://www.w3.org/2000/svg" width="512" height="512" viewBox="0 0 512 512"><defs><linearGradient id="bg" x2="1" y2="1"><stop stop-color="#173854"/><stop offset="1" stop-color="#091A2C"/></linearGradient><linearGradient id="feather" x2=".2" y2="1"><stop stop-color="#FFE18A"/><stop offset=".55" stop-color="#FFB14A"/><stop offset="1" stop-color="#F67855"/></linearGradient></defs><rect x="16" y="16" width="480" height="480" rx="112" fill="url(#bg)"/><path d="M360 94C257 104 173 181 166 293l60-41-48 74 62-26-43 68c104-30 179-144 163-274Z" fill="url(#feather)"/><path d="M142 412c31-119 96-211 184-280-71 90-113 156-147 250Z" fill="#64DFD2"/><path d="M249 337h112" fill="none" stroke="#A1B4CB" stroke-width="13" stroke-linecap="round"/><path d="M233 374h96" fill="none" stroke="#A1B4CB" stroke-width="13" stroke-linecap="round" opacity=".65"/></svg>'''
def make():
 p=ROOT/'assets/icons';p.mkdir(parents=True,exist_ok=True);(p/'master_icon.svg').write_text(svg,encoding='utf-8')
 # Supersampled native raster representation of the same feather silhouette.
 s=4; im=Image.new('RGBA',(512*s,512*s)); d=ImageDraw.Draw(im)
 def points(ps):return [(int(x*s),int(y*s)) for x,y in ps]
 d.rounded_rectangle((16*s,16*s,496*s,496*s),112*s,fill='#10283d')
 d.polygon(points([(360,94),(301,107),(246,141),(204,183),(178,233),(166,293),(226,252),(178,326),(240,300),(197,368),(254,338),(306,289),(342,226),(365,155)]),fill='#ffbb59')
 d.polygon(points([(142,412),(168,333),(210,257),(264,193),(326,132),(281,194),(233,270),(179,382)]),fill='#64dfd2')
 for x,y,x2,alpha in [(249,337,361,255),(233,374,329,165)]:d.line((x*s,y*s,x2*s,y*s),fill=(161,180,203,alpha),width=13*s)
 im=im.resize((512,512),Image.Resampling.LANCZOS);im.save(p/'icon-512.png');im.save(p/'app.ico',sizes=[(16,16),(24,24),(32,32),(48,48),(64,64),(128,128),(256,256)])
 w=ROOT/'assets/website';w.mkdir(parents=True,exist_ok=True);im.save(w/'icon.webp',quality=95)
if __name__=='__main__':make()
