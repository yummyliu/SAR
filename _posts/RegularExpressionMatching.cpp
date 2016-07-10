class Solution {
public:
    bool isMatch(string s, string p) {
        return helper(s,p,0,0);
    }
    bool helper(const string& s, const string& p, int ps, int pp){
        if (pp == p.length())
            return ps == s.length();
        if (pp ==  p.length()-1 || p[pp+1] !='*')
        {
            if (ps==s.length() || s[ps]!=p[pp]&&p[pp]!='.')
            {
                return false;
            }else
                return helper(s,p,ps+1,pp+1);
        }

        // p[pp] == '*'
        while (ps < s.length() &&(s[ps]==p[pp] || p[pp]=='.')){
            if (helper(s,p,ps,pp+2))
                return true;
            ps++;
        }
        return helper(s,p,ps,pp+2);
    }
};